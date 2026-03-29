#pragma once
#include <ranges>
#include <string>
#include <vector>

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

auto reportFatalError(std::string_view error_message) -> void;

} // namespace util
