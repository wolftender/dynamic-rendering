#pragma once
#include <array>
#include <optional>

namespace util {

template <typename T, size_t kCapacity> class CircularBufferQueue final {
public:
    CircularBufferQueue() = default;
    ~CircularBufferQueue() = default;

    auto capacity() const -> size_t { return kCapacity; }
    auto fill() const -> size_t { return fill_; }
    auto empty() const -> bool { return 0ull == fill_; }
    auto full() const -> bool { return kCapacity == fill_; }

    auto push(T v) -> void {
        if (full()) {
            return;
        }

        const auto index = (begin_ + fill_) % kCapacity;
        storage_[index] = std::move(v);

        fill_++;
    }

    auto pop() -> std::optional<T> {
        if (empty()) {
            return std::nullopt;
        }

        auto value = std::move(storage_[begin_]);
        begin_ = (begin_ + 1) % kCapacity;
        fill_--;

        return value;
    }

private:
    size_t begin_ = 0ull, fill_ = 0ull;
    std::array<T, kCapacity> storage_;
};

} // namespace util
