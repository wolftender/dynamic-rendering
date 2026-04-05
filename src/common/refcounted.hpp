#pragma once
#include <cassert>
#include <concepts>

namespace util {

template <typename T> class RefCountedPtr final {
public:
    using InterfaceType = T;

    static auto create(T *other) -> RefCountedPtr {
        RefCountedPtr<T> ptr;
        ptr.attach(other);
        return ptr;
    }

    RefCountedPtr() noexcept : ptr_{nullptr} {}
    RefCountedPtr(std::nullptr_t) noexcept : ptr_{nullptr} {}

    template <class U> RefCountedPtr(U *other) noexcept : ptr_{other} { addRefImpl(); }
    RefCountedPtr(const RefCountedPtr &other) noexcept : ptr_{other.ptr_} { addRefImpl(); }

    template <class U>
        requires std::convertible_to<U *, T *>
    RefCountedPtr(const RefCountedPtr<U> &other) noexcept : ptr_{other.ptr_} {
        addRefImpl();
    }

    RefCountedPtr(RefCountedPtr &&other) noexcept : ptr_{nullptr} {
        if (this != reinterpret_cast<RefCountedPtr *>(&reinterpret_cast<unsigned char &>(other))) {
            swap(other);
        }
    }

    template <class U>
        requires std::convertible_to<U *, T *>
    RefCountedPtr(RefCountedPtr<U> &&other) noexcept : ptr_{other.ptr_} {
        other.ptr_ = nullptr;
    }

    ~RefCountedPtr() noexcept { releaseImpl(); }

    auto operator=(std::nullptr_t) noexcept -> RefCountedPtr & {
        releaseImpl();
        return *this;
    }

    auto operator=(T *other) noexcept -> RefCountedPtr & {
        if (ptr_ != other) {
            RefCountedPtr(other).swap(*this);
        }

        return *this;
    }

    template <typename U> auto operator=(U *other) noexcept -> RefCountedPtr & {
        RefCountedPtr(other).swap(*this);
        return *this;
    }

    auto operator=(const RefCountedPtr &other) noexcept -> RefCountedPtr & {
        if (ptr_ != other.ptr_) {
            RefCountedPtr(other).swap(*this);
        }

        return *this;
    }

    template <class U> auto operator=(const RefCountedPtr<U> &other) noexcept -> RefCountedPtr & {
        RefCountedPtr(other).swap(*this);
        return *this;
    }

    auto operator=(RefCountedPtr &&other) noexcept -> RefCountedPtr & {
        RefCountedPtr(static_cast<RefCountedPtr &&>(other)).swap(*this);
        return *this;
    }

    template <class U> auto operator=(RefCountedPtr<U> &&other) noexcept -> RefCountedPtr & {
        RefCountedPtr(static_cast<RefCountedPtr<U> &&>(other)).swap(*this);
        return *this;
    }

    operator T *() const { return ptr_; }
    auto operator->() const noexcept -> T * { return ptr_; }
    auto operator&() -> T ** { return &ptr_; }

    auto swap(RefCountedPtr &&other) noexcept -> void {
        T *tmp = ptr_;
        ptr_ = other.ptr_;
        other.ptr_ = tmp;
    }

    auto swap(RefCountedPtr &other) noexcept -> void {
        T *tmp = ptr_;
        ptr_ = other.ptr_;
        other.ptr_ = tmp;
    }

    [[nodiscard]] auto get() const noexcept -> T * { return ptr_; }
    [[nodiscard]] auto getAddressOf() const noexcept -> T *const * { return &ptr_; }
    [[nodiscard]] auto getAddressOf() noexcept -> T ** { return &ptr_; }

    [[nodiscard]] auto releaseAndGetAddressOf() noexcept -> T ** {
        releaseImpl();
        return &ptr_;
    }

    auto detach() noexcept -> T * {
        T *ptr = ptr_;
        ptr_ = nullptr;

        return ptr;
    }

    auto attach(InterfaceType *other) -> void {
        if (nullptr != ptr_) {
            [[maybe_unused]] auto ref = ptr_->release();
            assert(ref != 0 || ptr_ != other);
        }

        ptr_ = other;
    }

    auto reset() -> uint32_t { return releaseImpl(); }

private:
    auto addRefImpl() noexcept -> void {
        if (ptr_ == nullptr) {
            return;
        }

        ptr_->addRef();
    }

    auto releaseImpl() noexcept -> uint32_t {
        uint32_t ref = 0;
        T *temp = ptr_;

        if (nullptr != temp) {
            ptr_ = nullptr;
            ref = temp->release();
        }

        return ref;
    }

    InterfaceType *ptr_ = nullptr;
};

} // namespace util
