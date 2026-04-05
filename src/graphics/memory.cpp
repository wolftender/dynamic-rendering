#include "common.hpp"
#include "vulkan.hpp"
#include "memory.hpp"

#include <vulkan/vulkan.h>
#include <volk.h>
#include <vk_mem_alloc.h>
#include <vulkan/vk_enum_string_helper.h>

namespace graphics {

Buffer::~Buffer() noexcept { destroy(); }

Buffer::Buffer(Buffer &&b) noexcept {
    destroy();
    device_ = b.device_;
    allocator_ = b.allocator_;

    buffer_ = b.buffer_;
    allocation_ = b.allocation_;
    allocation_info_ = std::move(b.allocation_info_);

    b.device_ = VK_NULL_HANDLE;
    b.allocator_ = VK_NULL_HANDLE;
    b.buffer_ = VK_NULL_HANDLE;
    b.allocation_ = VK_NULL_HANDLE;
}

auto Buffer::operator=(Buffer &&b) noexcept -> Buffer & {
    if (this != &b) {
        destroy();
        device_ = b.device_;
        allocator_ = b.allocator_;

        buffer_ = b.buffer_;
        allocation_ = b.allocation_;
        allocation_info_ = std::move(b.allocation_info_);

        b.device_ = VK_NULL_HANDLE;
        b.allocator_ = VK_NULL_HANDLE;
        b.buffer_ = VK_NULL_HANDLE;
        b.allocation_ = VK_NULL_HANDLE;
    }

    return *this;
}

auto Buffer::memPropFlags() const -> VkMemoryPropertyFlags {
    VkMemoryPropertyFlags props;
    vmaGetAllocationMemoryProperties(allocator_, allocation_, &props);

    return props;
}

auto Buffer::flush(VkDeviceSize offset, VkDeviceSize size) const {
    VK_CHECK_ERROR(vmaFlushAllocation(allocator_, allocation_, offset, size));
}

auto Buffer::deviceAddress() const -> VkDeviceAddress {
    VkBufferDeviceAddressInfo addr_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = buffer_,
    };

    return vkGetBufferDeviceAddress(device_, &addr_info);
}

auto Buffer::destroy() noexcept -> void {
    if (VK_NULL_HANDLE != buffer_) {
        vmaDestroyBuffer(allocator_, buffer_, allocation_);
    }

    buffer_ = VK_NULL_HANDLE;
    allocation_ = VK_NULL_HANDLE;
    allocator_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;

    allocation_info_ = {};
}

Image::View::~View() noexcept { destroy(); }

Image::View::View(View &&v) noexcept {
    destroy();
    image_view_ = v.image_view_;
    image_ = v.image_;
    device_ = v.device_;

    v.image_view_ = VK_NULL_HANDLE;
    v.image_ = nullptr;
    v.device_ = VK_NULL_HANDLE;
}

auto Image::View::operator=(View &&v) noexcept -> View & {
    if (this != &v) {
        destroy();

        image_view_ = v.image_view_;
        image_ = v.image_;
        device_ = v.device_;

        v.image_view_ = VK_NULL_HANDLE;
        v.image_ = nullptr;
        v.device_ = VK_NULL_HANDLE;
    }

    return *this;
}

auto Image::View::destroy() noexcept -> void {
    if (VK_NULL_HANDLE != image_view_) {
        vkDestroyImageView(device_, image_view_, nullptr);

        image_view_ = VK_NULL_HANDLE;
        image_ = nullptr;
        device_ = VK_NULL_HANDLE;
    }
}

Image::~Image() noexcept { destroy(); }

Image::Image(Image &&i) noexcept {
    destroy();

    device_ = i.device_;
    allocator_ = i.allocator_;
    image_ = i.image_;
    allocation_ = i.allocation_;
    allocation_info_ = std::move(i.allocation_info_);
    format_ = i.format_;

    i.device_ = VK_NULL_HANDLE;
    i.allocator_ = VK_NULL_HANDLE;
    i.image_ = VK_NULL_HANDLE;
    i.allocation_ = VK_NULL_HANDLE;
}

auto Image::operator=(Image &&i) noexcept -> Image & {
    if (this != &i) {
        destroy();

        device_ = i.device_;
        allocator_ = i.allocator_;
        image_ = i.image_;
        allocation_ = i.allocation_;
        allocation_info_ = std::move(i.allocation_info_);
        format_ = i.format_;

        i.device_ = VK_NULL_HANDLE;
        i.allocator_ = VK_NULL_HANDLE;
        i.image_ = VK_NULL_HANDLE;
        i.allocation_ = VK_NULL_HANDLE;
    }

    return *this;
}

auto Image::createView(VkImageViewType type, VkFormat format, VkImageAspectFlags aspect_flags) const -> View {
    VkImageViewCreateInfo view_desc = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = nullptr,
        .image = image_,
        .viewType = type,
        .format = format,
        .subresourceRange =
            {
                .aspectMask = aspect_flags,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };

    VkImageView view = VK_NULL_HANDLE;
    VK_CHECK_ERROR(vkCreateImageView(device_, &view_desc, nullptr, &view));

    View result;
    result.image_view_ = view;
    result.image_ = this;
    result.device_ = device_;

    return result;
}

auto Image::memPropFlags() const -> VkMemoryPropertyFlags {
    VkMemoryPropertyFlags props;
    vmaGetAllocationMemoryProperties(allocator_, allocation_, &props);

    return props;
}

auto Image::flush(VkDeviceSize offset, VkDeviceSize size) const {
    VK_CHECK_ERROR(vmaFlushAllocation(allocator_, allocation_, offset, size));
}

auto Image::destroy() noexcept -> void {
    if (VK_NULL_HANDLE != image_) {
        vmaDestroyImage(allocator_, image_, allocation_);
    }

    allocator_ = VK_NULL_HANDLE;
    allocation_ = VK_NULL_HANDLE;
    image_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;

    allocation_info_ = {};
}

auto MemoryHelper::create(Context *context) -> std::unique_ptr<MemoryHelper> {
    std::unique_ptr<MemoryHelper> memory{new (std::nothrow) MemoryHelper()};
    if (!memory) {
        LogError("vulkan: failed to allocate memory helper");
        util::reportFatalError("vulkan: failed to allocate memory helper");

        return nullptr;
    }

    memory->context_ = context;

    // initialize the allocator
    memory->allocator_funcs_ = VmaVulkanFunctions{
        .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr = vkGetDeviceProcAddr,
        .vkCreateBuffer = vkCreateBuffer,
        .vkCreateImage = vkCreateImage,
    };

    VmaAllocatorCreateInfo allocator_info = {
        .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
        .physicalDevice = context->physicalDevice(),
        .device = context->device(),
        .pVulkanFunctions = &memory->allocator_funcs_,
        .instance = context->instance(),
        .vulkanApiVersion = VK_API_VERSION_1_3,
    };

    VK_CHECK_ERROR(vmaCreateAllocator(&allocator_info, &memory->allocator_));
    LogInfo("vulkan: vma was initialized");

    // initialize timeline semaphore for upload timeline
    VkSemaphoreTypeCreateInfo timeline_create_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
        .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
    };

    VkSemaphoreCreateInfo semaphore_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = &timeline_create_info,
    };

    VK_CHECK_ERROR(vkCreateSemaphore(memory->context_->device(), &semaphore_info, nullptr, &memory->upload_semaphore_));

    // command pool for memory helper
    VkCommandPoolCreateInfo command_pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = memory->context_->graphicsQueueFamily(),
    };

    VK_CHECK_ERROR(
        vkCreateCommandPool(memory->context_->device(), &command_pool_info, nullptr, &memory->command_pool_));

    // command buffer for memory management helper
    VkCommandBufferAllocateInfo command_buffer_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = memory->command_pool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    VK_CHECK_ERROR(
        vkAllocateCommandBuffers(memory->context_->device(), &command_buffer_info, &memory->command_buffer_));

    LogInfo("vulkan: memory helper initialized");
    return memory;
}

MemoryHelper::~MemoryHelper() noexcept {
    LogInfo("vulkan: memory helper destroyed");

    vkDestroySemaphore(context_->device(), upload_semaphore_, nullptr);
    vkDestroyCommandPool(context_->device(), command_pool_, nullptr);

    if (VK_NULL_HANDLE != allocator_) {
        vmaDestroyAllocator(allocator_);
        allocator_ = VK_NULL_HANDLE;
    }
}

auto MemoryHelper::beginCommandBuffer() const -> void {
    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    VK_CHECK_ERROR(vkBeginCommandBuffer(command_buffer_, &begin_info));

    LogInfo("vulkan: memory helper begin recording command buffer");
}

auto MemoryHelper::submitCommandBuffer(VkSemaphore semaphore, uint64_t signal_value) const -> void {
    VK_CHECK_ERROR(vkEndCommandBuffer(command_buffer_));

    VkTimelineSemaphoreSubmitInfo timeline_info = {
        .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
        .signalSemaphoreValueCount = 1,
        .pSignalSemaphoreValues = &signal_value,
    };

    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = &timeline_info,
        .commandBufferCount = 1,
        .pCommandBuffers = &command_buffer_,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &semaphore,
    };

    VK_CHECK_ERROR(vkQueueSubmit(context_->graphicsQueue(), 1, &submit_info, VK_NULL_HANDLE));
    LogInfo("vulkan: memory helper submit command buffer");

    VkSemaphoreWaitInfo wait_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .semaphoreCount = 1,
        .pSemaphores = &upload_semaphore_,
        .pValues = &upload_timeline_,
    };

    VK_CHECK_ERROR(vkWaitSemaphores(context_->device(), &wait_info, UINT64_MAX));
    VK_CHECK_ERROR(vkResetCommandPool(context_->device(), command_pool_, 0));

    LogInfo("vulkan: memory helper end command submission logic");
}

auto MemoryHelper::createBuffer(VkBufferUsageFlags usage, std::span<const uint8_t> data, VkDeviceSize size) const
    -> Buffer {
    if (size == 0) {
        size = data.size();
    }

    assert(size >= data.size() && "invalid buffer size requested");

    VkBufferCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateInfo alloc_create_info = {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    };

    VmaAllocation allocation = VK_NULL_HANDLE;
    VkBuffer vk_buffer = VK_NULL_HANDLE;
    VmaAllocationInfo allocation_info = {};

    VK_CHECK_ERROR(
        vmaCreateBuffer(allocator_, &create_info, &alloc_create_info, &vk_buffer, &allocation, &allocation_info));

    Buffer buffer;
    buffer.device_ = context_->device();
    buffer.allocator_ = allocator_;
    buffer.buffer_ = vk_buffer;
    buffer.allocation_ = allocation;
    buffer.allocation_info_ = std::move(allocation_info);

    auto mem_prop_flags = buffer.memPropFlags();
    if (mem_prop_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        void *mapped_memory;
        VK_CHECK_ERROR(vmaMapMemory(allocator_, buffer.allocation_, &mapped_memory));

        ::memcpy(mapped_memory, data.data(), data.size());

        vmaUnmapMemory(allocator_, buffer.allocation_);
        VK_CHECK_ERROR(vmaFlushAllocation(allocator_, buffer.allocation_, 0, VK_WHOLE_SIZE));
    } else {
        // use staging buffer for upload as the memory is not host-mappable
        auto staging_buffer = createStagingBuffer(data.size());
        void *mapped_memory = staging_buffer.cpuMappedPointer();

        ::memcpy(mapped_memory, data.data(), data.size());

        VK_CHECK_ERROR(vmaFlushAllocation(allocator_, buffer.allocation_, 0, VK_WHOLE_SIZE));

        runOnTransferQueue([&](VkCommandBuffer command_buffer) {
            VkBufferCopy copy_info = {
                .srcOffset = 0,
                .dstOffset = 0,
                .size = data.size(),
            };

            vkCmdCopyBuffer(command_buffer, staging_buffer.buffer(), buffer.buffer(), 1, &copy_info);
        });
    }

    return buffer;
}

auto MemoryHelper::createStagingBuffer(VkDeviceSize size) const -> Buffer {
    VkBufferCreateInfo staging_buffer_desc = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateInfo staging_alloc_info = {
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };

    VmaAllocation allocation = VK_NULL_HANDLE;
    VkBuffer vk_buffer = VK_NULL_HANDLE;
    VmaAllocationInfo allocation_info = {};

    VK_CHECK_ERROR(vmaCreateBuffer(
        allocator_, &staging_buffer_desc, &staging_alloc_info, &vk_buffer, &allocation, &allocation_info));

    Buffer buffer;
    buffer.device_ = context_->device();
    buffer.allocator_ = allocator_;
    buffer.buffer_ = vk_buffer;
    buffer.allocation_ = allocation;
    buffer.allocation_info_ = std::move(allocation_info);

    return buffer;
}

auto MemoryHelper::createDeviceBuffer(VkBufferUsageFlags usage, VkDeviceSize size) const -> Buffer {
    VkBufferCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateInfo alloc_create_info = {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    };

    VmaAllocation allocation = VK_NULL_HANDLE;
    VkBuffer vk_buffer = VK_NULL_HANDLE;
    VmaAllocationInfo allocation_info = {};

    VK_CHECK_ERROR(
        vmaCreateBuffer(allocator_, &create_info, &alloc_create_info, &vk_buffer, &allocation, &allocation_info));

    Buffer buffer;
    buffer.device_ = context_->device();
    buffer.allocator_ = allocator_;
    buffer.buffer_ = vk_buffer;
    buffer.allocation_ = allocation;
    buffer.allocation_info_ = std::move(allocation_info);

    return buffer;
}

auto MemoryHelper::createSharedBuffer(VkBufferUsageFlags usage, VkDeviceSize size) const -> Buffer {
    VkBufferCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateInfo alloc_create_info = {
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };

    VmaAllocation allocation = VK_NULL_HANDLE;
    VkBuffer vk_buffer = VK_NULL_HANDLE;
    VmaAllocationInfo allocation_info = {};

    VK_CHECK_ERROR(
        vmaCreateBuffer(allocator_, &create_info, &alloc_create_info, &vk_buffer, &allocation, &allocation_info));

    Buffer buffer;
    buffer.device_ = context_->device();
    buffer.allocator_ = allocator_;
    buffer.buffer_ = vk_buffer;
    buffer.allocation_ = allocation;
    buffer.allocation_info_ = std::move(allocation_info);

    return buffer;
}

auto MemoryHelper::createImage(const VkImageCreateInfo &image_info) const -> Image {
    VmaAllocationCreateInfo alloc_create_info = {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    };

    VkImage vk_image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo allocation_info = {};

    VK_CHECK_ERROR(
        vmaCreateImage(allocator_, &image_info, &alloc_create_info, &vk_image, &allocation, &allocation_info));

    Image image;
    image.device_ = context_->device();
    image.allocator_ = allocator_;
    image.image_ = vk_image;
    image.allocation_ = allocation;
    image.allocation_info_ = std::move(allocation_info);
    image.format_ = image_info.format;

    return image;
}

auto MemoryHelper::createImage(
    VkFormat format, VkImageUsageFlags usage, VkImageType type, const VkExtent3D &extent) const -> Image {
    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = type,
        .format = format,
        .extent = extent,
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
    };

    return createImage(image_info);
}

auto MemoryHelper::createImageRgba(VkImageUsageFlags usage, VkExtent2D extent, std::span<const uint8_t> data) const
    -> Image {
    VkDeviceSize image_size = extent.width * extent.height * 4;
    assert(image_size <= data.size() && "invalid data span supplied");

    auto staging_buffer = createStagingBuffer(image_size);
    void *mapped_memory = staging_buffer.cpuMappedPointer();

    ::memcpy(mapped_memory, data.data(), image_size);

    VkImageCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_SRGB,
        .extent = VkExtent3D{extent.width, extent.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    };

    VmaAllocationCreateInfo alloc_create_info = {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    };

    VkImage vk_image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo allocation_info = {};

    VK_CHECK_ERROR(
        vmaCreateImage(allocator_, &create_info, &alloc_create_info, &vk_image, &allocation, &allocation_info));

    Image image;
    image.device_ = context_->device();
    image.allocator_ = allocator_;
    image.image_ = vk_image;
    image.allocation_ = allocation;
    image.allocation_info_ = std::move(allocation_info);
    image.format_ = VK_FORMAT_R8G8B8A8_SRGB;

    runOnTransferQueue([&](VkCommandBuffer command_buffer) {
        VkImageSubresourceRange range = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };

        VkImageMemoryBarrier2 barrier_unknown_to_transfer_dst{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = 0,
            .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .image = image.image(),
            .subresourceRange = range,
        };

        VkDependencyInfo dep_unknown_to_transfer_dst = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier_unknown_to_transfer_dst,
        };

        vkCmdPipelineBarrier2(command_buffer, &dep_unknown_to_transfer_dst);

        VkBufferImageCopy image_copy = {
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource =
                VkImageSubresourceLayers{
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            .imageExtent = VkExtent3D{extent.width, extent.height, 1},
        };

        vkCmdCopyBufferToImage(
            command_buffer, staging_buffer.buffer(), image.image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
            &image_copy);

        VkImageMemoryBarrier2 barrier_transfer_dst_to_shader_read = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .image = image.image(),
            .subresourceRange = range,
        };

        VkDependencyInfo dep_transfer_dst_to_shader_read = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier_transfer_dst_to_shader_read,
        };

        vkCmdPipelineBarrier2(command_buffer, &dep_transfer_dst_to_shader_read);
    });

    return image;
}

} // namespace graphics
