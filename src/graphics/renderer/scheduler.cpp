#include "scheduler.hpp"
#include "logger.hpp"

#include "graphics/common.hpp"
#include "graphics/vulkan.hpp"

#include <volk.h>

namespace graphics {

auto RendererScheduler::create(Context *context) -> std::unique_ptr<RendererScheduler> {
    std::unique_ptr<RendererScheduler> scheduler{new (std::nothrow) RendererScheduler()};
    if (!scheduler) {
        LogError("vulkan: cannot allocate renderer scheduler object");
        return {};
    }

    scheduler->context_ = context;

    // allocate command buffers
    VkCommandPoolCreateInfo command_pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = scheduler->context_->graphicsQueueFamily(),
    };

    VK_CHECK_ERROR(
        vkCreateCommandPool(scheduler->context_->device(), &command_pool_info, nullptr, &scheduler->command_pool_));

    VkCommandBufferAllocateInfo command_buffers_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = scheduler->command_pool_,
        .commandBufferCount = kNumFramesInFlight,
    };

    std::array<VkCommandBuffer, kNumFramesInFlight> command_buffers;

    VK_CHECK_ERROR(
        vkAllocateCommandBuffers(scheduler->context_->device(), &command_buffers_info, command_buffers.data()));

    for (size_t i = 0; i < kNumFramesInFlight; ++i) {
        scheduler->per_frame_data_[i].command_buffer = command_buffers[i];
    }

    VkSemaphoreCreateInfo semaphore_desc = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };

    VkFenceCreateInfo fence_desc = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    // synchronization structures
    for (size_t i = 0; i < kNumFramesInFlight; ++i) {
        auto &frame = scheduler->per_frame_data_[i];
        VK_CHECK_ERROR(vkCreateFence(scheduler->context_->device(), &fence_desc, nullptr, &frame.fence));
        VK_CHECK_ERROR(
            vkCreateSemaphore(scheduler->context_->device(), &semaphore_desc, nullptr, &frame.present_semaphore));
    }

    return scheduler;
}

RendererScheduler::~RendererScheduler() noexcept {}

} // namespace graphics
