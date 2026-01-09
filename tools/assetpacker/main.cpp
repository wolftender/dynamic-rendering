#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <ios>
#include <iterator>
#include <optional>
#include <ranges>
#include <sstream>
#include <vector>
#include <filesystem>

#include <lz4.h>
#include <fmt/base.h>

enum class Result { Fail = 0, Success = 1 };

template <typename T> inline constexpr auto vec_char_ptr(const std::vector<T> &v) -> const char * {
    return reinterpret_cast<const char *>(v.data());
}

template <typename T> inline constexpr auto vec_char_ptr_mut(std::vector<T> &v) -> char * {
    return reinterpret_cast<char *>(v.data());
}

auto has_flag(int argc, char *const *argv, const std::string_view flag_short, const std::string_view flag_long) -> bool;
auto get_filenames(int argc, char *const *argv) -> std::optional<std::pair<std::string, std::string>>;
auto read_file_to_buffer(const std::string &file_name, std::vector<uint8_t> &buffer) -> Result;
auto compress_to_buffer(
    const std::vector<uint8_t> &input_buffer, std::vector<uint8_t> &output_buffer, size_t &result_size) -> Result;
auto decompress_to_buffer(
    const std::vector<uint8_t> &input_buffer, std::vector<uint8_t> &output_buffer, size_t &result_size) -> Result;

template <std::ranges::random_access_range Range>
    requires std::is_convertible_v<std::ranges::range_value_t<Range>, const char>
auto write_buffer_to_file(const std::string &file_name, const Range &range) -> Result {
    const auto data_ptr = std::ranges::data(range);
    const auto data_size = std::ranges::size(range);

    std::ofstream fs{file_name, std::ios::binary};
    if (!fs.good()) {
        return Result::Fail;
    }

    fs.write(reinterpret_cast<const char *>(data_ptr), data_size);
    return Result::Success;
}

auto make_variable_name(const std::string &file_name) -> std::string;
template <std::ranges::random_access_range Range>
    requires std::is_convertible_v<std::ranges::range_value_t<Range>, const char>
auto write_file_to_header(const std::string &file_name, const Range &range) -> Result {
    constexpr size_t kColumnWidth = 10;
    constexpr size_t kTabWidth = 4;

    const auto data_ptr = std::ranges::data(range);
    const auto data_size = std::ranges::size(range);

    std::ofstream fs{file_name};
    if (!fs.good()) {
        return Result::Fail;
    }

    const auto var_name = make_variable_name(file_name);
    const std::string indent_str(kTabWidth, ' ');

    fs << "#pragma once" << std::endl;
    fs << "#include <array>" << std::endl;
    fs << "#include <cstdint>" << std::endl;
    fs << std::endl;
    fs << "constexpr std::array<uint8_t, " << data_size << "> k" << var_name << " = {" << std::endl << indent_str;

    for (size_t byte_num = 0; byte_num < data_size; ++byte_num) {
        uint8_t byte = data_ptr[byte_num];
        fs << "0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<uint32_t>(byte);
        if ((byte_num + 1) % kColumnWidth == 0) {
            fs << "," << std::endl << indent_str;
        } else if (byte_num == data_size - 1) {
            fs << std::endl;
        } else {
            fs << ", ";
        }
    }

    fs << "};" << std::endl;
    return Result::Success;
}

auto main(int argc, char **argv) -> int {
    const auto file_args = get_filenames(argc, argv);
    if (!file_args.has_value()) {
        fmt::println(stderr, "incorrect usage: {} <input_file> <output_file>", argv[0]);
        return EXIT_FAILURE;
    }

    const bool use_header_output = has_flag(argc, argv, "-h", "--header");
    const bool use_unpacker = has_flag(argc, argv, "-u", "--unpack");
    const bool use_passthrough_only = has_flag(argc, argv, "-c", "--copy");

    const auto input_file_name = file_args.value().first;
    const auto output_file_name = file_args.value().second;

    std::vector<uint8_t> input_buffer;
    auto res = read_file_to_buffer(input_file_name, input_buffer);
    if (Result::Success != res) {
        fmt::println(stderr, "error: cannot read file {}", input_file_name);
        return EXIT_FAILURE;
    }

    if (!use_passthrough_only) {
        std::vector<uint8_t> output_buffer;
        size_t output_size;

        if (use_unpacker) {
            res = decompress_to_buffer(input_buffer, output_buffer, output_size);
            if (Result::Success != res) {
                fmt::println(stderr, "error: failed to decompress file {}", input_file_name);
                return EXIT_FAILURE;
            }
        } else {
            res = compress_to_buffer(input_buffer, output_buffer, output_size);
            if (Result::Success != res) {
                fmt::println(stderr, "error: failed to compress file {}", input_file_name);
                return EXIT_FAILURE;
            }
        }

        if (use_header_output) {
            res = write_file_to_header(std::string{output_file_name}, output_buffer | std::views::take(output_size));
            if (Result::Success != res) {
                fmt::println(stderr, "error: failed to write file {}", output_file_name);
                return EXIT_FAILURE;
            }
        } else {
            res = write_buffer_to_file(std::string{output_file_name}, output_buffer | std::views::take(output_size));
            if (Result::Success != res) {
                fmt::println(stderr, "error: failed to write file {}", output_file_name);
                return EXIT_FAILURE;
            }
        }
    } else {
        if (use_header_output) {
            res = write_file_to_header(std::string{output_file_name}, input_buffer);
            if (Result::Success != res) {
                fmt::println(stderr, "error: failed to write file {}", output_file_name);
                return EXIT_FAILURE;
            }
        } else {
            res = write_buffer_to_file(std::string{output_file_name}, input_buffer);
            if (Result::Success != res) {
                fmt::println(stderr, "error: failed to write file {}", output_file_name);
                return EXIT_FAILURE;
            }
        }
    }

    return EXIT_SUCCESS;
}

auto has_flag(int argc, char *const *argv, const std::string_view flag_short, const std::string_view flag_long)
    -> bool {
    const auto flag_short_len = flag_short.size();
    const auto flag_long_len = flag_long.size();

    for (int i = 1; i < argc; ++i) {
        const char *arg_value = argv[i];
        const size_t arg_len = strlen(arg_value);

        if (strncmp(arg_value, flag_short.data(), std::min(flag_short_len, arg_len)) == 0) {
            return true;
        }

        if (strncmp(arg_value, flag_long.data(), std::min(flag_long_len, arg_len)) == 0) {
            return true;
        }
    }

    return false;
}

auto get_filenames(int argc, char *const *argv) -> std::optional<std::pair<std::string, std::string>> {
    std::optional<std::string> input_name;
    std::optional<std::string> output_name;

    for (int i = 1; i < argc; ++i) {
        const char *arg_value = argv[i];
        if (arg_value[0] != '-') {
            if (!input_name.has_value()) {
                input_name = std::string{arg_value};
            } else if (!output_name.has_value()) {
                output_name = std::string{arg_value};
            } else {
                break;
            }
        }
    }

    if (input_name.has_value() && output_name.has_value()) {
        return std::make_pair(input_name.value(), output_name.value());
    }

    return {};
}

auto read_file_to_buffer(const std::string &file_name, std::vector<uint8_t> &buffer) -> Result {
    std::ifstream fs{file_name, std::ios::binary};
    if (!fs.good()) {
        return Result::Fail;
    }

    fs.seekg(0, std::ios::end);
    const auto file_size = static_cast<size_t>(fs.tellg());
    fs.seekg(0, std::ios::beg);

    buffer.resize(file_size);
    fs.read(vec_char_ptr_mut(buffer), file_size);

    return Result::Success;
}

constexpr uint32_t kAssetHeader = 0x415354cc;

auto write_uint32(uint8_t *buffer, uint32_t value) -> void {
    buffer[0] = static_cast<char>(value & 0xff);
    buffer[1] = static_cast<char>((value >> 8) & 0xff);
    buffer[2] = static_cast<char>((value >> 16) & 0xff);
    buffer[3] = static_cast<char>((value >> 24) & 0xff);
}

auto read_uint32(const uint8_t *buffer) -> uint32_t {
    return 0U | (static_cast<uint32_t>(buffer[0]) << 0) | (static_cast<uint32_t>(buffer[1]) << 8) |
           (static_cast<uint32_t>(buffer[2]) << 16) | (static_cast<uint32_t>(buffer[3]) << 24);
}

auto compress_to_buffer(
    const std::vector<uint8_t> &input_buffer, std::vector<uint8_t> &output_buffer, size_t &result_size) -> Result {
    const auto max_compressed_size = LZ4_compressBound(static_cast<int>(input_buffer.size()));
    const auto decompressed_size = static_cast<uint32_t>(static_cast<int>(input_buffer.size()));
    const int header_size = 2 * sizeof(uint32_t);

    output_buffer.resize(max_compressed_size + header_size);
    write_uint32(output_buffer.data(), kAssetHeader);
    write_uint32(output_buffer.data() + sizeof(uint32_t), decompressed_size);

    const auto compressed_size = LZ4_compress_default(vec_char_ptr(input_buffer),
        vec_char_ptr_mut(output_buffer) + header_size, static_cast<int>(input_buffer.size()), max_compressed_size);

    if (compressed_size < 0) {
        fmt::println(stderr, "error: compression error {}", compressed_size);
        return Result::Fail;
    }

    fmt::println(stdout, "compressed asset {} bytes -> {} bytes", decompressed_size, compressed_size);

    result_size = compressed_size + header_size;
    return Result::Success;
}

auto decompress_to_buffer(
    const std::vector<uint8_t> &input_buffer, std::vector<uint8_t> &output_buffer, size_t &result_size) -> Result {
    const int header_size = 2 * sizeof(uint32_t);

    if (input_buffer.size() < header_size) {
        return Result::Fail;
    }

    const uint32_t header_magic = read_uint32(input_buffer.data());
    const uint32_t header_filesize = read_uint32(input_buffer.data() + sizeof(uint32_t));

    if (header_magic != kAssetHeader) {
        fmt::println(stderr, "error: decompression error, magic mismatch");
        return Result::Fail;
    }

    // allocate enough size for the file
    output_buffer.resize(header_filesize);
    const auto decompressed_size = LZ4_decompress_safe(vec_char_ptr(input_buffer) + header_size,
        vec_char_ptr_mut(output_buffer), static_cast<int>(input_buffer.size()) - header_size, header_filesize);

    if (decompressed_size < 0) {
        fmt::println(stderr, "error: decompression error {}", decompressed_size);
        return Result::Fail;
    }

    if (static_cast<uint32_t>(decompressed_size) != header_filesize) {
        fmt::println(
            stderr, "error: decompressed size {} does not match header size {}", decompressed_size, header_filesize);
    }

    result_size = decompressed_size;
    return Result::Success;
}

constexpr inline auto is_ascii_lowercase(const char ch) -> bool { return (ch >= 'a' && ch <= 'z'); }
constexpr inline auto is_ascii_uppercase(const char ch) -> bool { return (ch >= 'A' && ch <= 'Z'); }
constexpr inline auto to_upper_case(const char ch) -> char { return is_ascii_lowercase(ch) ? 'A' + (ch - 'a') : ch; }
constexpr inline auto to_lower_case(const char ch) -> char { return is_ascii_uppercase(ch) ? 'a' + (ch - 'A') : ch; }

auto make_variable_name(const std::string &file_name) -> std::string {
    namespace fs = std::filesystem;
    const auto path = fs::path{file_name};
    const auto file = path.stem().string();

    bool next_upper = true;
    std::ostringstream ss;
    for (const auto ch : file) {
        if (ch == '_') {
            ss << ch;
            next_upper = true;
        } else if (is_ascii_lowercase(ch)) {
            if (next_upper) {
                ss << to_upper_case(ch);
            } else {
                ss << ch;
            }

            next_upper = false;
        } else if (is_ascii_uppercase(ch)) {
            ss << ch;
            next_upper = false;
        } else if (ss.tellp() != 0 && ch >= '0' && ch <= '9') {
            ss << ch;
        } else {
            ss << '_';
        }
    }

    return ss.str();
}
