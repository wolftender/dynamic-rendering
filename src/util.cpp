#include <iterator>

#include <fmt/format.h>
#include <lz4.h>

#ifdef _WIN32
#include <Windows.h>
#endif

#include "util.hpp"

namespace util {
constexpr uint32_t kAssetHeader = 0x415354cc;

auto writeUint32(uint8_t *buffer, uint32_t value) -> void {
    buffer[0] = static_cast<char>(value & 0xff);
    buffer[1] = static_cast<char>((value >> 8) & 0xff);
    buffer[2] = static_cast<char>((value >> 16) & 0xff);
    buffer[3] = static_cast<char>((value >> 24) & 0xff);
}

auto readUint32(const uint8_t *buffer) -> uint32_t {
    return 0U | (static_cast<uint32_t>(buffer[0]) << 0) | (static_cast<uint32_t>(buffer[1]) << 8) |
           (static_cast<uint32_t>(buffer[2]) << 16) | (static_cast<uint32_t>(buffer[3]) << 24);
}

auto decompressBuffer(const uint8_t *buffer, size_t buffer_size, std::vector<uint8_t> &decompressed) -> Result {
    const int header_size = 2 * sizeof(uint32_t);

    if (buffer_size < header_size) {
        return Result::Failure;
    }

    const uint32_t header_magic = readUint32(buffer);
    const uint32_t header_filesize = readUint32(buffer + sizeof(uint32_t));

    if (header_magic != kAssetHeader) {
        return Result::Failure;
    }

    // allocate enough size for the file
    decompressed.resize(header_filesize);
    const auto decompressed_size = LZ4_decompress_safe(
        reinterpret_cast<const char *>(buffer) + (sizeof(uint32_t) * 2), reinterpret_cast<char *>(decompressed.data()),
        static_cast<int>(buffer_size) - header_size, header_filesize);

    if (decompressed_size < 0) {
        return Result::Failure;
    }

    if (static_cast<uint32_t>(decompressed_size) != header_filesize) {
        fmt::println(
            stderr, "error: decompressed size {} does not match header size {}", decompressed_size, header_filesize);
    }

    return Result::Success;
}

auto decompressBuffer(std::span<uint8_t> buffer, std::vector<uint8_t> &decompressed) -> Result {
    return decompressBuffer(std::data(buffer), std::size(buffer), decompressed);
}

#ifdef _WIN32
// windows users most likely don't like stderr, as with subsystem:windows they
// will not see it and it makes debugging a chore on users pc
auto reportFatalError(std::string_view error_message) -> void {
    std::wstring wstr_msg;
    auto result =
        ::MultiByteToWideChar(CP_UTF8, 0, error_message.data(), static_cast<int>(error_message.size()), nullptr, 0);

    if (result <= 0) {
        goto fatal_conversion_error;
    }

    // why +10? this is how its done on msdn, im not sure why
    // https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-multibytetowidechar
    wstr_msg.resize(result + 10);
    result = ::MultiByteToWideChar(
        CP_UTF8, 0, error_message.data(), static_cast<int>(error_message.size()), wstr_msg.data(),
        static_cast<int>(wstr_msg.size()));

    if (result <= 0) {
        goto fatal_conversion_error;
    }

    MessageBoxW(NULL, wstr_msg.data(), L"fatal error", MB_OK | MB_ICONERROR);

fatal_conversion_error:
    MessageBoxW(
        NULL,
        L"fatal error has occured when converting the error message from "
        L"utf-8, this is probably a memory "
        L"corruption",
        L"fatal error", MB_OK | MB_ICONERROR);
}
#else
// for unix users stderr is natural place for fatal error to be reported, so
// just let it be printed there
void reportFatalError(std::string_view error_message) { fmt::println(stderr, "fatal error: {}", error_message); }
#endif

} // namespace util
