#pragma once
#include <optional>

#include "utility.hpp"
#include "byteutils.hpp"

#include <glm/glm.hpp>

namespace util::bytes {

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
        const auto x = read<T>();
        const auto y = read<T>();

        if (!x.has_value() || !y.has_value()) {
            return std::nullopt;
        }

        return glm::vec<2, T>{*x, *y};
    }

    template <typename T = f32> auto readVec3() -> std::optional<glm::vec<3, T>> {
        const auto x = read<T>();
        const auto y = read<T>();
        const auto z = read<T>();

        if (!x.has_value() || !y.has_value() || !z.has_value()) {
            return std::nullopt;
        }

        return glm::vec<3, T>{*x, *y, *z};
    }

    template <typename T = f32> auto readVec4() -> std::optional<glm::vec<4, T>> {
        const auto x = read<T>();
        const auto y = read<T>();
        const auto z = read<T>();
        const auto w = read<T>();

        if (!x.has_value() || !y.has_value() || !z.has_value() || !w.has_value()) {
            return std::nullopt;
        }

        return glm::vec<4, T>{*x, *y, *z, *w};
    }

    template <typename T = f32> auto readQuat() -> std::optional<glm::qua<T>> {
        const auto x = read<T>();
        const auto y = read<T>();
        const auto z = read<T>();
        const auto w = read<T>();

        if (!x.has_value() || !y.has_value() || !z.has_value() || !w.has_value()) {
            return std::nullopt;
        }

        return glm::qua<T>{*w, *x, *y, *z}; // this constructor has order WXYZ
    }

private:
    std::span<const u8> data_span_;
    u64 ptr_;
};

} // namespace util::bytes