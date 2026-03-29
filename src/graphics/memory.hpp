#pragma once
#include "vulkan.hpp"

namespace graphics {

class Context;

class Buffer final {
public:
    Buffer() = default;
    ~Buffer() noexcept;

    Buffer(const Buffer &) = delete;
    auto operator=(const Buffer &) = delete;

    Buffer(Buffer &&buffer) noexcept;
    auto operator=(Buffer &&buffer) noexcept -> Buffer &;

    auto buffer() const -> VkBuffer { return buffer_; }
    auto addrOf() const -> const VkBuffer * { return &buffer_; }

    auto memPropFlags() const -> VkMemoryPropertyFlags;
    auto flush(VkDeviceSize offset = 0ull, VkDeviceSize size = VK_WHOLE_SIZE) const;

    auto cpuMappedPointer() const -> void * { return allocation_info_.pMappedData; }
    auto deviceAddress() const -> VkDeviceAddress;

private:
    auto destroy() noexcept -> void;

    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;

    VkBuffer buffer_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VmaAllocationInfo allocation_info_ = {};

    friend class MemoryHelper;
};

class Image final {
public:
    class View final {
    public:
        View() = default;
        ~View() noexcept;

        View(const View &) = delete;
        auto operator=(const View &) = delete;

        View(View &&view) noexcept;
        auto operator=(View &&view) noexcept -> View &;

        auto view() const -> VkImageView { return image_view_; }
        auto image() const -> const Image * { return image_; }

    private:
        auto destroy() noexcept -> void;

        const Image *image_ = nullptr;
        VkDevice device_ = VK_NULL_HANDLE;
        VkImageView image_view_ = VK_NULL_HANDLE;

        friend class Image;
    };

    Image() = default;
    ~Image() noexcept;

    Image(const Image &image) = delete;
    auto operator=(const Image &image) = delete;

    Image(Image &&image) noexcept;
    auto operator=(Image &&image) noexcept -> Image &;

    auto image() const -> VkImage { return image_; }
    auto addrOf() const -> const VkImage * { return &image_; }
    auto format() const -> VkFormat { return format_; }

    auto createView(VkImageViewType type, VkFormat format, VkImageAspectFlags aspect_flags) const -> View;
    auto memPropFlags() const -> VkMemoryPropertyFlags;
    auto flush(VkDeviceSize offset = 0ull, VkDeviceSize size = VK_WHOLE_SIZE) const;

private:
    auto destroy() noexcept -> void;

    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;

    VkImage image_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VmaAllocationInfo allocation_info_ = {};

    VkFormat format_;

    friend class View;
    friend class MemoryHelper;
};

class MemoryHelper final {
public:
    ~MemoryHelper() noexcept;

    MemoryHelper(const MemoryHelper &&) = delete;
    auto operator=(const MemoryHelper &&) = delete;

    MemoryHelper(MemoryHelper &&) noexcept = delete;
    auto operator=(MemoryHelper &&) noexcept = delete;

    auto createBuffer(VkBufferUsageFlags usage, std::span<const uint8_t> data, VkDeviceSize size = 0) const -> Buffer;
    auto createStagingBuffer(VkDeviceSize size) const -> Buffer;
    auto createDeviceBuffer(VkBufferUsageFlags usage, VkDeviceSize size) const -> Buffer;
    auto createSharedBuffer(VkBufferUsageFlags usage, VkDeviceSize size) const -> Buffer;

    auto createImage(const VkImageCreateInfo &image_info) const -> Image;
    auto createImage(VkFormat format, VkImageUsageFlags usage, VkImageType type, const VkExtent3D &extent) const
        -> Image;
    auto createImageRgba(VkImageUsageFlags usage, VkExtent2D extent, std::span<const uint8_t> data) const -> Image;

    template <std::invocable<VkCommandBuffer> F> auto runOnTransferQueue(F runner) const {
        beginCommandBuffer();
        runner(command_buffer_);
        submitCommandBuffer(upload_semaphore_, ++upload_timeline_);
    }

private:
    static auto create(Context *context) -> std::unique_ptr<MemoryHelper>;

    MemoryHelper() = default;

    auto beginCommandBuffer() const -> void;
    auto submitCommandBuffer(VkSemaphore semaphore, uint64_t signal_value) const -> void;

    Context *context_ = nullptr;

    VmaVulkanFunctions allocator_funcs_ = {};
    VmaAllocator allocator_ = VK_NULL_HANDLE;

    mutable uint64_t upload_timeline_ = 0ull;
    VkSemaphore upload_semaphore_ = VK_NULL_HANDLE;
    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer_ = VK_NULL_HANDLE;

    friend class Context;
};

} // namespace graphics
