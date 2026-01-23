#pragma once

#include <ranges>
#include <span>
#include <string>
#include <optional>

#include <glm/glm.hpp>

namespace util {

template <typename T, typename V>
concept TypedForwardRange = std::ranges::forward_range<T> && std::convertible_to<std::ranges::range_value_t<T>, V>;

template <typename T, typename V>
concept TypedRandomAccessRange =
    std::ranges::random_access_range<T> && std::convertible_to<std::ranges::range_value_t<T>, V>;

template <typename T, typename V>
concept TypedContiguousRange =
    std::ranges::random_access_range<T> && std::convertible_to<std::ranges::range_value_t<T>, V>;

template <typename T>
concept StringValue =
    std::same_as<T, std::string> || std::same_as<T, std::string_view> || std::convertible_to<T, char *>;

template <typename T>
concept MapLike = requires(T m, const typename T::key_type &k) {
    typename T::key_type;
    typename T::mapped_type;
    { m.find(k) } -> std::convertible_to<typename T::const_iterator>;
};

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

enum class Result { eFailure = 0, eSuccess = 1 };

namespace bytes {

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

auto bufferCrc32(std::span<const u8> data) -> u32;

class BinaryReader final {
public:
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

    template <typename T = f32> auto readVec2() -> std::optional<glm::vec<2, T>> {
        const auto x = read<f32>();
        const auto y = read<f32>();

        if (!x.has_value() || !y.has_value()) {
            return std::nullopt;
        }

        return glm::vec<2, T>{*x, *y};
    }

    template <typename T = f32> auto readVec3() -> std::optional<glm::vec<3, T>> {
        const auto x = read<f32>();
        const auto y = read<f32>();
        const auto z = read<f32>();

        if (!x.has_value() || !y.has_value() || !z.has_value()) {
            return std::nullopt;
        }

        return glm::vec<3, T>{*x, *y, *z};
    }

    template <typename T = f32> auto readVec4() -> std::optional<glm::vec<4, T>> {
        const auto x = read<f32>();
        const auto y = read<f32>();
        const auto z = read<f32>();
        const auto w = read<f32>();

        if (!x.has_value() || !y.has_value() || !z.has_value() || !w.has_value()) {
            return std::nullopt;
        }

        return glm::vec<4, T>{*x, *y, *z, *w};
    }

    template <typename T = f32> auto readQuat() -> std::optional<glm::qua<T>> {
        const auto x = read<f32>();
        const auto y = read<f32>();
        const auto z = read<f32>();
        const auto w = read<f32>();

        if (!x.has_value() || !y.has_value() || !z.has_value() || !w.has_value()) {
            return std::nullopt;
        }

        return glm::qua<T>{*x, *y, *z, *w};
    }

private:
    std::span<const u8> data_span_;
    u64 ptr_;
};

} // namespace bytes

auto reportFatalError(std::string_view error_message) -> void;

} // namespace util
