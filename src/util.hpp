#pragma once

#include <ranges>
#include <span>
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

enum class Result { Failure = 0, Success = 1 };

auto decompressBuffer(const uint8_t *buffer, size_t buffer_size, std::vector<uint8_t> &decompressed) -> Result;
auto decompressBuffer(std::span<uint8_t> buffer, std::vector<uint8_t> &decompressed) -> Result;

template <TypedContiguousRange<uint8_t> R>
auto decompressBuffer(const R &buffer, std::vector<uint8_t> &decompressed) -> Result {
    const auto data_size = std::ranges::size(buffer);
    const auto data_ptr = std::ranges::data(buffer);

    return decompressBuffer(data_ptr, data_size, decompressed);
}

template <TypedContiguousRange<uint8_t> R> auto decompressBuffer(const R &buffer) -> std::vector<uint8_t> {
    std::vector<uint8_t> decompressed;
    decompressBuffer(buffer, decompressed);

    return decompressed;
}

auto reportFatalError(std::string_view error_message) -> void;

} // namespace util
