#include "assets.hpp"
#include "logger.hpp"

#include "common/byteutils.hpp"
#include "common/binaryreader.hpp"

#include <lz4.h>

#ifdef _WIN32
#include <Windows.h>
#include "resource.h"
#endif

namespace asset {

constexpr uint32_t kArchiveMagic = 0x415243cc;
constexpr uint32_t kAssetMagic = 0x415354cc;

using util::bytes::BinaryReader;

auto decompressBuffer(
    std::span<const uint8_t> input_buffer, std::vector<uint8_t> &output_buffer, size_t decompressed_size) -> Result {
    // allocate enough size for the file
    output_buffer.resize(decompressed_size);
    const auto decompressed_bytes = LZ4_decompress_safe(
        util::spanCharPtr(input_buffer), util::vecCharPtrMut(output_buffer), static_cast<int>(input_buffer.size()),
        decompressed_size);

    if (decompressed_size < 0) {
        LogError("error: decompression error {}", decompressed_size);
        return Result::eFailure;
    }

    if (decompressed_size != static_cast<uint64_t>(decompressed_bytes)) {
        LogError("error: invalid decompressed data size {} != {}", decompressed_bytes, decompressed_size);
        return Result::eFailure;
    }

    return Result::eSuccess;
}

auto ArchiveReader::create(std::span<const uint8_t> buffer) -> std::unique_ptr<ArchiveReader> {
    std::unique_ptr<ArchiveReader> reader{new (std::nothrow) ArchiveReader()};
    if (!reader) {
        LogError("asset: failed to create archive reader: out of memory");
        return nullptr;
    }

    reader->buffer_ = buffer;
    BinaryReader br{reader->buffer_};

    const auto magic = br.read<uint32_t>();
    if (!magic.has_value() || magic.value() != kArchiveMagic) {
        LogError("asset: invalid magic number");
        return nullptr;
    }

    const auto header_size = br.read<uint64_t>();
    const auto body_size = br.read<uint64_t>();

    if (!header_size.has_value() || !body_size.has_value()) {
        LogError("asset: invalid asset archive file format");
        return nullptr;
    }

    const auto header_buffer = br.readBuffer(header_size.value());
    if (!header_buffer.has_value()) {
        LogError("asset: invalid archive format, not enough bytes for header");
        return nullptr;
    }

    const auto body_buffer = br.readBuffer(body_size.value());
    if (!body_buffer.has_value()) {
        LogError("asset: invalid archive format, not enough bytes for body");
        return nullptr;
    }

    reader->header_buffer_ = header_buffer.value();
    reader->body_buffer_ = body_buffer.value();

    const auto res = reader->buildDirectory();
    if (Result::eSuccess != res) {
        LogError("asset: failed to build asset directory");
        return nullptr;
    }

    return reader;
}

auto ArchiveReader::getContentView(uint64_t pointer, uint64_t size) const -> std::optional<std::span<const uint8_t>> {
    if (pointer > body_buffer_.size() || pointer + size > body_buffer_.size()) {
        return std::nullopt;
    }

    return std::span<const uint8_t>{body_buffer_.data() + pointer, size};
}

auto ArchiveReader::buildDirectory() -> Result {
    // read all files from the header and store to internal map
    BinaryReader hr{header_buffer_};

    for (auto asset_header = hr.read<uint32_t>(); asset_header.has_value(); asset_header = hr.read<uint32_t>()) {
        const auto file_id = file_entries_.size();

        if (asset_header.value() != kAssetMagic) {
            LogError("asset: invalid asset header detected");
            return Result::eFailure;
        }

        const auto arc_crc = hr.read<uint32_t>();
        const auto arc_compressed_size = hr.read<uint32_t>();
        const auto arc_real_size = hr.read<uint32_t>();
        const auto arc_offset = hr.read<uint64_t>();
        const auto arc_name_len = hr.read<uint32_t>();

        if (!arc_crc.has_value() || !arc_compressed_size.has_value() || !arc_real_size.has_value() ||
            !arc_offset.has_value() || !arc_name_len.has_value()) {
            LogError("asset: corrupted header format");
            return Result::eFailure;
        }

        const auto arc_name_buf = hr.readBuffer(arc_name_len.value());
        if (!arc_name_buf.has_value()) {
            LogError("asset: corrupted header name format");
            return Result::eFailure;
        }

        FileEntry entry;
        entry.crc32 = arc_crc.value();
        entry.compressed_size = arc_compressed_size.value();
        entry.decompressed_size = arc_real_size.value();
        entry.ptr_start = arc_offset.value();
        entry.name = std::string{reinterpret_cast<const char *>(arc_name_buf->data()), arc_name_buf->size()};

        const auto file_view = getContentView(entry.ptr_start, entry.compressed_size);
        if (!file_view.has_value()) {
            LogError("asset: invalid file pointer in archive for file id={}, {}", file_id, entry.name);
            return Result::eFailure;
        }

        // we can check not compressed assets here
        if (entry.compressed_size == entry.decompressed_size) {
            const auto data_crc = util::bytes::bufferCrc32(file_view.value());
            if (data_crc != entry.crc32) {
                LogError("asset: invalid file crc in archive for file id={}, {}", file_id, entry.name);
                return Result::eFailure;
            }
        }

        file_entry_by_name_.insert(std::make_pair(entry.name, file_id));
        file_entries_.emplace_back(std::move(entry));
    }

    return Result::eSuccess;
}

auto ArchiveReader::getFileContent(std::string_view name) const -> std::optional<std::vector<uint8_t>> {
    auto iter = file_entry_by_name_.find(name);
    if (file_entry_by_name_.end() == iter) {
        return std::nullopt;
    }

    return getFileContent(iter->second);
}

auto ArchiveReader::getFileContent(uint64_t file_id) const -> std::optional<std::vector<uint8_t>> {
    const auto &entry = file_entries_[file_id];
    const auto view = getContentView(entry.ptr_start, entry.compressed_size);

    // no compression
    if (entry.compressed_size == entry.decompressed_size) {
        return std::vector<uint8_t>{view->begin(), view->end()};
    }

    std::vector<uint8_t> decompressed;
    decompressed.resize(entry.decompressed_size);

    const auto res = decompressBuffer(view.value(), decompressed, entry.decompressed_size);
    if (Result::eSuccess != res) {
        LogError("asset: failed to decompress asset id={}, {}", file_id, entry.name);
        return std::nullopt;
    }

    // verify crc after decompression
    const auto crc = util::bytes::bufferCrc32(decompressed);
    if (crc != entry.crc32) {
        LogError("asset: failed to verify crc for compressed asset id={}, {}", file_id, entry.name);
        return std::nullopt;
    }

    return decompressed;
}

#ifdef _WIN32

class MainArchive::Impl final {
public:
    static auto create() -> std::unique_ptr<Impl>;

    Impl(const Impl &) = delete;
    auto operator=(const Impl &) = delete;

    Impl(Impl &&) = delete;
    auto operator=(Impl &&) = delete;

    auto data() const -> std::span<const uint8_t> { return data_; }
    auto reader() const -> const ArchiveReader & { return *reader_.get(); }

private:
    Impl() = default;

    HRSRC resource_ = nullptr;
    HGLOBAL memory_ = nullptr;

    std::span<const uint8_t> data_;
    std::unique_ptr<ArchiveReader> reader_;
};

auto MainArchive::Impl::create() -> std::unique_ptr<Impl> {
    std::unique_ptr<Impl> impl{new (std::nothrow) Impl()};
    if (!impl) {
        LogError("asset: windows impl failed to allocate");
        return nullptr;
    }

    const auto module = ::GetModuleHandle(NULL);

    impl->resource_ = ::FindResource(nullptr, MAKEINTRESOURCE(IDR_ASSETS), RT_RCDATA);
    if (!impl->resource_) {
        LogError("asset: windows impl failed to find main archive resource");
        return nullptr;
    }

    impl->memory_ = ::LoadResource(module, impl->resource_);
    if (!impl->memory_) {
        LogError("asset: windows impl failed to load main archive resource");
        return nullptr;
    }

    const size_t rc_size = ::SizeofResource(module, impl->resource_);
    const uint8_t *rc_data = reinterpret_cast<const uint8_t *>(::LockResource(impl->memory_));

    impl->data_ = std::span<const uint8_t>{rc_data, rc_size};
    impl->reader_ = ArchiveReader::create(impl->data_);

    if (!impl->reader_) {
        LogError("asset: windows impl failed to read main archive");
        return nullptr;
    }

    return impl;
}

#endif

auto MainArchive::create() -> std::unique_ptr<MainArchive> {
    std::unique_ptr<MainArchive> archive{new (std::nothrow) MainArchive()};
    if (!archive) {
        LogError("asset: failed to allocate main archive handler");
        return nullptr;
    }

    archive->impl_ = Impl::create();
    if (!archive->impl_) {
        LogError("asset: failed to initialize main archive impl");
        return nullptr;
    }

    return archive;
}

auto MainArchive::reader() const -> const ArchiveReader & { return impl_->reader(); }

MainArchive::MainArchive() {}
MainArchive::~MainArchive() {}

} // namespace asset
