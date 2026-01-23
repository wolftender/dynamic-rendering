#include <variant>
#include <fstream>
#include <filesystem>

#include <fmt/base.h>
#include <fmt/format.h>

#include "util.hpp"
#include "archive.hpp"

enum class ProgramMode {
    ePack,
    eUnpack,
    eInspect,
};

enum class OutputMode {
    eBinary,
    eHeader,
    eSourceAndHeader,
};

auto stringToProgramMode(std::string_view sv) -> std::optional<ProgramMode> {
    if ("pack" == sv) {
        return ProgramMode::ePack;
    } else if ("unpack" == sv) {
        return ProgramMode::eUnpack;
    } else if ("inspect" == sv) {
        return ProgramMode::eInspect;
    }

    return std::nullopt;
}

auto stringToOutputMode(std::string_view sv) -> std::optional<OutputMode> {
    if ("binary" == sv) {
        return OutputMode::eBinary;
    } else if ("header" == sv) {
        return OutputMode::eHeader;
    } else if ("source" == sv) {
        return OutputMode::eSourceAndHeader;
    }

    return std::nullopt;
}

struct InspectDescription {
    std::string input_file;
};

struct UnpackDescription {
    std::string input_file;
    std::string output_directory;
};

struct PackDescription {
    struct FileEntry {
        std::string file_name;
        bool use_compression;
    };

    std::vector<FileEntry> input_files;
    std::string output_file;

    OutputMode output_mode;
};

using ProgramOptions = std::variant<InspectDescription, UnpackDescription, PackDescription, std::monostate>;

/// parse command line options for archive description
auto parseCommandLine(int argc, char **argv) -> ProgramOptions;

auto inspect(const InspectDescription &desc) -> Result;
auto unpack(const UnpackDescription &desc) -> Result;
auto pack(const PackDescription &desc) -> Result;

/// utility to make a camel cased constant name from a filename
auto makeVariableName(const std::string &file_name) -> std::string;

/// write a binary buffer to header file
template <std::ranges::random_access_range Range>
    requires std::is_convertible_v<std::ranges::range_value_t<Range>, const char>
auto writeBufferToHeader(const std::string &file_name, const Range &range) -> Result;

/// write a binary buffer to header AND source file
template <std::ranges::random_access_range Range>
    requires std::is_convertible_v<std::ranges::range_value_t<Range>, const char>
auto writeBufferToHeaderAndSrc(const std::string &file_name, const Range &range) -> Result;

auto main(int argc, char **argv) -> int {
    const auto options = parseCommandLine(argc, argv);
    Result exit_result = Result::Fail;

    std::visit(
        overload{
            [&exit_result](const InspectDescription &desc) { exit_result = inspect(desc); },
            [&exit_result](const UnpackDescription &desc) { exit_result = unpack(desc); },
            [&exit_result](const PackDescription &desc) { exit_result = pack(desc); },
            [&exit_result]([[maybe_unused]] const std::monostate &) { exit_result = Result::Fail; },
        },
        options);

    return exit_result == Result::Success ? EXIT_SUCCESS : EXIT_FAILURE;
}

auto inspect(const InspectDescription &desc) -> Result {
    std::vector<uint8_t> buffer;
    const auto res = readFileToBuffer(desc.input_file, buffer);

    if (Result::Success != res) {
        fmt::println(stderr, "error: {} is not accessible", desc.input_file);
        return Result::Fail;
    }

    auto reader = ArchiveReader::create(buffer);
    if (!reader) {
        fmt::println(stderr, "error: {} is not a valid archive", desc.input_file);
        return Result::Fail;
    }

    fmt::println(stdout, "CRC32 | compressed size | raw size | offset | name");
    for (const auto &entry : reader->getFileList()) {
        fmt::println(
            stdout, "{} | {} | {} | {} | {}", entry.crc32, entry.compressed_size, entry.decompressed_size,
            entry.ptr_start, entry.name);
    }

    return Result::Success;
}

auto unpack(const UnpackDescription &desc) -> Result {
    std::vector<uint8_t> buffer;
    const auto res = readFileToBuffer(desc.input_file, buffer);

    if (Result::Success != res) {
        fmt::println(stderr, "error: {} is not accessible", desc.input_file);
        return Result::Fail;
    }

    auto reader = ArchiveReader::create(buffer);
    if (!reader) {
        fmt::println(stderr, "error: {} is not a valid archive", desc.input_file);
        return Result::Fail;
    }

    fmt::println(stdout, "CRC32 | compressed size | raw size | offset | name");
    for (u64 file_id = 0ull; file_id < reader->getFileList().size(); ++file_id) {
        const auto &entry = reader->getFileList()[file_id];
        const auto output_path = std::filesystem::path{desc.output_directory} / entry.name;

        fmt::println(stdout, "decompressing {}...", entry.name);

        const auto buffer = reader->getFileContent(file_id);
        if (!buffer.has_value()) {
            fmt::println(stderr, "error: cannot decompress {}", entry.name);
            continue;
        }

        auto res = writeBufferToFile(output_path.string(), buffer.value());
        if (Result::Success != res) {
            fmt::println(stderr, "error: cannot write {} to {}", entry.name, output_path.string());
            continue;
        }
    }

    return Result::Success;
}

auto pack(const PackDescription &desc) -> Result {
    ArchiveWriter writer;
    for (const auto &entry : desc.input_files) {
        std::filesystem::path path{entry.file_name};
        if (!std::filesystem::exists(path)) {
            fmt::println(stderr, "error: {} does not exist, skipping", entry.file_name);
            continue;
        }

        Result res;
        std::vector<u8> buffer;

        res = readFileToBuffer(entry.file_name, buffer);
        if (Result::Success != res) {
            fmt::println(stderr, "error: {} not accessible, skipping", entry.file_name);
            continue;
        }

        if (entry.use_compression) {
            res = writer.appendCompressed(path.filename().generic_string(), buffer);
            if (Result::Success != res) {
                fmt::println(stderr, "error: {} compression failed, skipping", entry.file_name);
                continue;
            }
        } else {
            writer.appendFile(path.filename().generic_string(), buffer);
        }

        fmt::println(
            stdout, "appended file to archive: {}, compressed: {}", entry.file_name,
            entry.use_compression ? "yes" : "no");
    }

    switch (desc.output_mode) {
    case OutputMode::eBinary: {
        std::ofstream fs{desc.output_file, std::ios::out | std::ios::binary};
        if (!fs.good()) {
            return Result::Fail;
        }

        writer.dump(fs);
        return Result::Success;
    }

    case OutputMode::eHeader: {
        return writeBufferToHeader(desc.output_file, writer.dump());
    }

    case OutputMode::eSourceAndHeader: {
        return writeBufferToHeaderAndSrc(desc.output_file, writer.dump());
    }

    default:
        fmt::println(stderr, "error: unsupported format");
        return Result::Fail;
    }
}

auto parseCommandLine(int argc, char **argv) -> ProgramOptions {
    std::vector<std::string_view> args{static_cast<size_t>(argc)};
    for (int i = 0; i < argc; ++i) {
        args[i] = std::string_view{argv[i]};
    }

    auto arg_iter = args.begin() + 1;
    if (args.end() == arg_iter) {
        fmt::println(stderr, "error: incorrect program usage, missing mode");
        return std::monostate{};
    }

    const auto mode = stringToProgramMode(*arg_iter);
    if (!mode.has_value()) {
        fmt::println(stderr, "error: incorrect program usage, unknown mode {}", *arg_iter);
        return std::monostate{};
    }

    switch (mode.value()) {
    case ProgramMode::eInspect: {
        arg_iter++;
        if (args.end() == arg_iter) {
            fmt::println(stderr, "error: icorrect program usage, expected file to inspect");
            return std::monostate{};
        }

        return InspectDescription{std::string{*arg_iter}};
    }

    case ProgramMode::eUnpack: {
        UnpackDescription desc;

        arg_iter++;
        if (args.end() == arg_iter) {
            fmt::println(stderr, "error: icorrect program usage, expected file to unpack");
            return std::monostate{};
        }

        desc.input_file = *arg_iter;

        arg_iter++;
        if (args.end() == arg_iter) {
            fmt::println(stderr, "error: icorrect program usage, expected output directory");
            return std::monostate{};
        }

        desc.output_directory = *arg_iter;
        return desc;
    }

    case ProgramMode::ePack: {
        PackDescription desc;

        arg_iter++;
        if (args.end() == arg_iter) {
            fmt::println(stderr, "error: icorrect program usage, expected export format");
            return std::monostate{};
        }

        auto format = stringToOutputMode(*arg_iter);
        if (!format.has_value()) {
            fmt::println(stderr, "error: icorrect program usage, invalid format {}", *arg_iter);
            return std::monostate{};
        }

        desc.output_mode = format.value();

        arg_iter++;
        if (args.end() == arg_iter) {
            fmt::println(stderr, "error: icorrect program usage, expected output file name");
            return std::monostate{};
        }

        desc.output_file = *arg_iter;
        arg_iter++;

        for (; args.end() != arg_iter; ++arg_iter) {
            std::string input_file{*arg_iter};
            bool compress_file = false;

            if (input_file.starts_with("c:")) {
                compress_file = true;
            } else if (input_file.starts_with("a:")) {
                compress_file = false;
            } else {
                fmt::println(stderr, "error: incorrect input file format: {}", input_file);
                return std::monostate{};
            }

            std::string filename = input_file.substr(2);
            desc.input_files.emplace_back(PackDescription::FileEntry{std::move(filename), compress_file});
        }

        return desc;
    }

    default:
        break;
    }

    return std::monostate{};
}

auto writeU32(uint8_t *buffer, uint32_t value) -> void {
    buffer[0] = static_cast<char>(value & 0xff);
    buffer[1] = static_cast<char>((value >> 8) & 0xff);
    buffer[2] = static_cast<char>((value >> 16) & 0xff);
    buffer[3] = static_cast<char>((value >> 24) & 0xff);
}

auto readU32(const uint8_t *buffer) -> uint32_t {
    return 0U | (static_cast<uint32_t>(buffer[0]) << 0) | (static_cast<uint32_t>(buffer[1]) << 8) |
           (static_cast<uint32_t>(buffer[2]) << 16) | (static_cast<uint32_t>(buffer[3]) << 24);
}

auto makeVariableName(const std::string &file_name) -> std::string {
    namespace fs = std::filesystem;
    const auto path = fs::path{file_name};
    const auto file = path.stem().string();

    bool next_upper = true;
    std::ostringstream ss;
    for (const auto ch : file) {
        if (ch == '_') {
            ss << ch;
            next_upper = true;
        } else if (isAsciiLowercase(ch)) {
            if (next_upper) {
                ss << toUppercase(ch);
            } else {
                ss << ch;
            }

            next_upper = false;
        } else if (isAsciiUppercase(ch)) {
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

template <std::ranges::random_access_range Range>
    requires std::is_convertible_v<std::ranges::range_value_t<Range>, const char>
auto writeBufferToHeader(const std::string &file_name, const Range &range) -> Result {
    constexpr size_t kColumnWidth = 10;
    constexpr size_t kTabWidth = 4;

    const auto data_ptr = std::ranges::data(range);
    const auto data_size = std::ranges::size(range);

    std::ofstream fs{file_name};
    if (!fs.good()) {
        return Result::Fail;
    }

    const auto var_name = makeVariableName(file_name);
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

template <std::ranges::random_access_range Range>
    requires std::is_convertible_v<std::ranges::range_value_t<Range>, const char>
auto writeBufferToHeaderAndSrc(const std::string &file_name, const Range &range) -> Result {
    constexpr size_t kColumnWidth = 10;
    constexpr size_t kTabWidth = 4;

    const auto data_ptr = std::ranges::data(range);
    const auto data_size = std::ranges::size(range);

    const auto cc_name = fmt::format("{}.cpp", file_name);
    const auto h_name = fmt::format("{}.hpp", file_name);

    std::ofstream fs_cc{cc_name};
    std::ofstream fs_h{h_name};

    if (!fs_cc.good() || !fs_h.good()) {
        return Result::Fail;
    }

    const auto var_name = makeVariableName(file_name);
    const std::string indent_str(kTabWidth, ' ');

    fs_h << "#pragma once" << std::endl;
    fs_h << "#include <cstdint>" << std::endl;
    fs_h << std::endl;
    fs_h << "extern const uint64_t k" << var_name << "_size;" << std::endl;
    fs_h << "extern const uint8_t k" << var_name << "[];" << std::endl;

    fs_cc << "#include \"" << h_name << "\"" << std::endl;
    fs_cc << std::endl;
    fs_cc << "const uint64_t k" << var_name << "_size = " << data_size << ";" << std::endl;
    fs_cc << "const uint8_t k" << var_name << "[k" << var_name << "_size] = {" << std::endl << indent_str;

    for (size_t byte_num = 0; byte_num < data_size; ++byte_num) {
        uint8_t byte = data_ptr[byte_num];
        fs_cc << "0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<uint32_t>(byte);
        if ((byte_num + 1) % kColumnWidth == 0) {
            fs_cc << "," << std::endl << indent_str;
        } else if (byte_num == data_size - 1) {
            fs_cc << std::endl;
        } else {
            fs_cc << ", ";
        }
    }

    fs_cc << "};" << std::endl;
    return Result::Success;
}
