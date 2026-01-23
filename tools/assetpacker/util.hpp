#pragma once
#include <cstdint>
#include <vector>
#include <ranges>
#include <span>
#include <optional>
#include <string>
#include <fstream>
#include <ios>

enum class Result { Fail = 0, Success = 1 };

template <typename... Ts> struct overload : Ts... {
    using Ts::operator()...;
};

template <typename T> inline constexpr auto spanCharPtr(const std::span<T> &v) -> const char * {
    return reinterpret_cast<const char *>(v.data());
}

template <typename T> inline constexpr auto spanCharPtrMut(std::span<T> &v) -> char * {
    return reinterpret_cast<char *>(v.data());
}

template <typename T> inline constexpr auto vecCharPtr(const std::vector<T> &v) -> const char * {
    return reinterpret_cast<const char *>(v.data());
}

template <typename T> inline constexpr auto vecCharPtrMut(std::vector<T> &v) -> char * {
    return reinterpret_cast<char *>(v.data());
}

// clang-format off
using u8    = uint8_t;
using u16   = uint16_t;
using u32   = uint32_t;
using u64   = uint64_t;
using s8    = int8_t;
using s16   = int16_t;
using s32   = int32_t;
using s64   = int64_t;
using f32   = float;
using f64   = double;
// clang-format on

template <typename T>
concept IsPrimitiveType = std::same_as<T, u8> || std::same_as<T, u16> || std::same_as<T, u32> || std::same_as<T, u64> ||
                          std::same_as<T, s8> || std::same_as<T, s16> || std::same_as<T, s32> || std::same_as<T, s64> ||
                          std::same_as<T, f32> || std::same_as<T, f64>;

template <typename R, typename T>
concept TypedRange = std::ranges::range<R> && std::same_as<std::ranges::range_value_t<R>, T>;

template <typename R, typename T>
concept TypedContiguousRange = std::ranges::contiguous_range<R> && std::same_as<std::ranges::range_value_t<R>, T>;

template <typename From, typename To>
concept CanBeCast = requires(From &&value) { static_cast<To>(value); };

class BinaryWriter final {
public:
    explicit BinaryWriter(size_t preallocateSize = 4096) { buffer_.reserve(preallocateSize); }
    ~BinaryWriter() = default;

    std::vector<u8> dumpBuffer() { return std::move(buffer_); }
    const std::vector<u8> &getBuffer() const { return buffer_; }

    template <TypedContiguousRange<s8> R> void writeBuffer(const R &buffer) {
        const auto begin = reinterpret_cast<const u8 *>(std::ranges::data(buffer));
        const auto end = begin + std::ranges::size(buffer);

        buffer_.insert(buffer_.end(), begin, end);
    }

    template <TypedContiguousRange<u8> R> void writeBuffer(const R &buffer) {
        const auto begin = std::ranges::data(buffer);
        const auto end = begin + std::ranges::size(buffer);

        buffer_.insert(buffer_.end(), begin, end);
    }

    template <std::ranges::range R>
        requires IsPrimitiveType<std::ranges::range_value_t<R>>
    void writeBuffer(const R &buffer) {
        using T = std::ranges::range_value_t<R>;
        constexpr size_t elementSize = sizeof(T);

        const auto numElements = std::ranges::size(buffer);
        const auto currentSize = buffer_.size();
        const auto targetSize = currentSize + (numElements * elementSize);

        if (targetSize > currentSize) {
            buffer_.reserve(targetSize);
        }

        for (const auto &element : buffer) {
            writeValueLittleEndian(element);
        }
    }

    template <CanBeCast<u8> U8> void writeU8(U8 value) { writeByte(static_cast<u8>(value)); }
    template <CanBeCast<s8> S8> void writeS8(S8 value) { writeByte(static_cast<s8>(value)); }
    template <CanBeCast<u16> U16> void writeU16(U16 value) { writeValueLittleEndian(static_cast<u16>(value)); }
    template <CanBeCast<s16> S16> void writeS16(S16 value) { writeValueLittleEndian(static_cast<s16>(value)); }
    template <CanBeCast<u32> U32> void writeU32(U32 value) { writeValueLittleEndian(static_cast<u32>(value)); }
    template <CanBeCast<s32> S32> void writeS32(S32 value) { writeValueLittleEndian(static_cast<s32>(value)); }
    template <CanBeCast<u64> U64> void writeU64(U64 value) { writeValueLittleEndian(static_cast<u64>(value)); }
    template <CanBeCast<s64> S64> void writeS64(S64 value) { writeValueLittleEndian(static_cast<s64>(value)); }
    template <CanBeCast<f32> F32> void writeF32(F32 value) { writeValueLittleEndian(static_cast<f32>(value)); }
    template <CanBeCast<f64> F64> void writeF64(F64 value) { writeValueLittleEndian(static_cast<f64>(value)); }

private:
    void writeByte(u8 byte) { buffer_.push_back(byte); }

    template <typename T> void writeValueLittleEndian(const T &value) {
        constexpr auto numBytes = sizeof(T);
        const u8 *begin = reinterpret_cast<const u8 *>(&value);
        const u8 *ptr = begin;

        for (size_t i = 0; i < numBytes; ++i) {
            writeByte(*ptr++);
        }
    }

    std::vector<uint8_t> buffer_;
};

class BinaryReader final {
public:
    enum class Result { eSuccess, eFailure };

    explicit BinaryReader(std::span<const u8> input_range) : data_span_{std::move(input_range)}, ptr_{0ull} {}
    ~BinaryReader() = default;

    BinaryReader(const BinaryReader &) = delete;
    auto operator=(const BinaryReader &) = delete;

    BinaryReader(BinaryReader &&) noexcept = default;
    auto operator=(BinaryReader &&) noexcept -> BinaryReader & = default;

    auto position() const -> u64 { return ptr_; }
    auto remaining() const -> u64;
    auto seek(u64 location) -> Result;
    auto readBuffer(u64 num_bytes) -> std::optional<std::span<const u8>>;

    template <IsPrimitiveType T> auto read() -> std::optional<T> {
        constexpr auto type_size = sizeof(T);
        const auto end_ptr = data_span_.size();

        if (ptr_ + type_size > end_ptr) {
            return std::nullopt;
        }

        // little endian data read
        T result = *(reinterpret_cast<const T *>(data_span_.data() + ptr_));

        ptr_ = ptr_ + type_size;
        return result;
    }

private:
    std::span<const u8> data_span_;
    u64 ptr_;
};

/// read file into a byte buffer
auto readFileToBuffer(const std::string &file_name, std::vector<uint8_t> &buffer) -> Result;

/// write binary file
template <std::ranges::random_access_range Range>
    requires std::is_convertible_v<std::ranges::range_value_t<Range>, const char>
auto writeBufferToFile(const std::string &file_name, const Range &range) -> Result {
    const auto data_ptr = std::ranges::data(range);
    const auto data_size = std::ranges::size(range);

    std::ofstream fs{file_name, std::ios::binary};
    if (!fs.good()) {
        return Result::Fail;
    }

    fs.write(reinterpret_cast<const char *>(data_ptr), data_size);
    return Result::Success;
}

constexpr inline auto isAsciiLowercase(const char ch) -> bool { return (ch >= 'a' && ch <= 'z'); }
constexpr inline auto isAsciiUppercase(const char ch) -> bool { return (ch >= 'A' && ch <= 'Z'); }
constexpr inline auto toUppercase(const char ch) -> char { return isAsciiLowercase(ch) ? 'A' + (ch - 'a') : ch; }
constexpr inline auto toLowercase(const char ch) -> char { return isAsciiUppercase(ch) ? 'a' + (ch - 'A') : ch; }
