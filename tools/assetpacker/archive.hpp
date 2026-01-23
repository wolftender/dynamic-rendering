#pragma once
#include <map>

#include "util.hpp"

constexpr u32 kArchiveHeader = 0x415243cc;
constexpr u32 kAssetHeader = 0x415354cc;

auto bufferCrc32(std::span<const u8> data) -> u32;

/// compress buffer
auto compressToBuffer(std::span<const u8> input_buffer, std::vector<u8> &output_buffer, size_t &result_size) -> Result;

/// decompress buffer
auto decompressToBuffer(std::span<const u8> input_buffer, std::vector<u8> &output_buffer, size_t decompressed_size)
    -> Result;

class ArchiveWriter final {
public:
    template <TypedContiguousRange<u8> Range> auto appendFile(std::string_view name, const Range &r) -> void {
        const auto data_ptr = std::ranges::data(r);
        const auto data_sz = std::ranges::size(r);

        appendFile(name, std::span<const u8>{data_ptr, data_sz});
    }

    template <TypedContiguousRange<u8> Range> auto appendCompressed(std::string_view name, const Range &r) -> Result {
        const auto data_ptr = std::ranges::data(r);
        const auto data_sz = std::ranges::size(r);

        return appendCompressed(name, std::span<const u8>{data_ptr, data_sz});
    }

    auto appendFile(std::string_view name, std::span<const u8> binary) -> void;
    auto appendCompressed(std::string_view name, std::span<const u8> binary) -> Result;

    auto getHeaderBuffer() const -> std::vector<u8> { return header_writer_.getBuffer(); }
    auto getBodyBuffer() const -> std::vector<u8> { return body_writer_.getBuffer(); }

    auto dump(std::ostream &stream) const -> void;
    auto dump() const -> std::vector<u8>;

private:
    BinaryWriter header_writer_;
    BinaryWriter body_writer_;
};

class ArchiveReader final {
public:
    struct FileEntry {
        std::string name;
        u32 crc32;
        u64 ptr_start;
        u32 compressed_size;
        u32 decompressed_size;
    };

    static auto create(std::span<const u8> buffer) -> std::unique_ptr<ArchiveReader>;

    ArchiveReader(const ArchiveReader &) = delete;
    auto operator=(const ArchiveReader &) = delete;

    ArchiveReader(ArchiveReader &&) noexcept = default;
    auto operator=(ArchiveReader &&) noexcept -> ArchiveReader & = default;

    auto getFileList() const -> const std::vector<FileEntry> & { return file_entries_; }
    auto getFileContent(std::string_view name) -> std::optional<std::vector<u8>>;
    auto getFileContent(u64 file_id) -> std::optional<std::vector<u8>>;

private:
    explicit ArchiveReader() = default;

    auto buildDirectory() -> Result;
    auto getContentView(u64 pointer, u64 size) const -> std::optional<std::span<const u8>>;

    std::span<const u8> buffer_;
    std::span<const u8> header_buffer_;
    std::span<const u8> body_buffer_;

    std::vector<FileEntry> file_entries_;
    std::map<std::string, u64, std::less<>> file_entry_by_name_;
};
