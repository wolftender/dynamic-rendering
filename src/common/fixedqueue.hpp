#include <array>
#include <optional>

#include "common/utility.hpp"

namespace util {

template <typename T, size_t kNumElements> class FixedSizeQueue final {
public:
    FixedSizeQueue() : fill_{0} {}
    ~FixedSizeQueue() noexcept = default;

    auto getSize() const -> uint32_t { return kNumElements; }
    auto getFill() const -> uint32_t { return fill_; }
    auto empty() const -> bool { return fill_ == 0; }
    auto full() const -> bool { return fill_ >= kNumElements; }

    auto peek() -> const T * {
        if (0 == fill_) {
            return nullptr;
        }

        return &buffer_[fill_ - 1];
    }

    auto push(T &&t) -> Result {
        if (kNumElements == fill_) {
            return Result::eFailure;
        }

        buffer_[fill_++] = std::move(t);
        return Result::eSuccess;
    }

    auto pop() -> std::optional<T> {
        if (fill_ == 0) {
            return std::nullopt;
        }

        return buffer_[--fill_];
    }

    auto clear() -> void { fill_ = 0; }

    auto operator[](const size_t index) const -> const std::optional<T> & { return buffer_[index]; }

private:
    std::array<std::optional<T>, kNumElements> buffer_;
    uint32_t fill_;
};

} // namespace util
