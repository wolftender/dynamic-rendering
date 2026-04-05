#pragma once
#include <cstdint>
#include <span>

namespace util::bytes {

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

template <typename T> auto align_ptr(T size, T align) -> T { return (size + align - 1) & ~(align - 1); }

template <typename T>
concept IsPrimitiveType = std::same_as<T, u8> || std::same_as<T, u16> || std::same_as<T, u32> || std::same_as<T, u64> ||
                          std::same_as<T, s8> || std::same_as<T, s16> || std::same_as<T, s32> || std::same_as<T, s64> ||
                          std::same_as<T, f32> || std::same_as<T, f64>;

auto bufferCrc32(std::span<const u8> data) -> u32;

} // namespace util::bytes
