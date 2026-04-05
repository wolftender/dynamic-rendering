#pragma once
#include <memory>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "common/utility.hpp"

namespace asset {

using util::Result;

/// decompress buffer
auto decompressBuffer(
    std::span<const uint8_t> input_buffer, std::vector<uint8_t> &output_buffer, size_t decompressed_size) -> Result;

template <util::TypedContiguousRange<uint8_t> R>
auto decompressBuffer(const R &range, std::vector<uint8_t> &output_buffer, size_t decompressed_size) -> Result {
    const auto data_ptr = std::ranges::data(range);
    const auto data_len = std::ranges::size(range);

    return decompressBuffer(std::span<uint8_t>{data_ptr, data_len}, output_buffer, decompressed_size);
}

class ArchiveReader final {
public:
    struct FileEntry {
        std::string name;
        uint32_t crc32;
        uint64_t ptr_start;
        uint32_t compressed_size;
        uint32_t decompressed_size;
    };

    static auto create(std::span<const uint8_t> buffer) -> std::unique_ptr<ArchiveReader>;

    ArchiveReader(const ArchiveReader &) = delete;
    auto operator=(const ArchiveReader &) = delete;

    ArchiveReader(ArchiveReader &&) noexcept = default;
    auto operator=(ArchiveReader &&) noexcept -> ArchiveReader & = default;

    auto getFileList() const -> const std::vector<FileEntry> & { return file_entries_; }
    auto getFileContent(std::string_view name) const -> std::optional<std::vector<uint8_t>>;
    auto getFileContent(uint64_t file_id) const -> std::optional<std::vector<uint8_t>>;

private:
    ArchiveReader() = default;

    auto buildDirectory() -> Result;
    auto getContentView(uint64_t pointer, uint64_t size) const -> std::optional<std::span<const uint8_t>>;

    std::span<const uint8_t> buffer_;
    std::span<const uint8_t> header_buffer_;
    std::span<const uint8_t> body_buffer_;

    std::vector<FileEntry> file_entries_;
    std::map<std::string, uint64_t, std::less<>> file_entry_by_name_;
};

// main asset archive
class MainArchive final {
public:
    static auto create() -> std::unique_ptr<MainArchive>;

    ~MainArchive();

    MainArchive(const MainArchive &) = delete;
    auto operator=(const MainArchive &) = delete;

    MainArchive(MainArchive &&) noexcept = default;
    auto operator=(MainArchive &&) noexcept -> MainArchive & = default;

    auto reader() const -> const ArchiveReader &;

private:
    MainArchive();

    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace asset
