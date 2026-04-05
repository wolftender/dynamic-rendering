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

    scheduler->createSwapchainData();
    return scheduler;
}

RendererScheduler::~RendererScheduler() noexcept {
    LogInfo("vulkan: releasing scheduler resources");
    VK_CHECK_ERROR(vkDeviceWaitIdle(context_->device()));

    if (VK_NULL_HANDLE != command_pool_) {
        vkDestroyCommandPool(context_->device(), command_pool_, nullptr);
        command_pool_ = VK_NULL_HANDLE;
    }

    for (size_t i = 0; i < kNumFramesInFlight; ++i) {
        auto &frame = per_frame_data_[i];

        if (VK_NULL_HANDLE != frame.fence) {
            vkDestroyFence(context_->device(), frame.fence, nullptr);
            frame.fence = VK_NULL_HANDLE;
        }

        if (VK_NULL_HANDLE != frame.present_semaphore) {
            vkDestroySemaphore(context_->device(), frame.present_semaphore, nullptr);
            frame.present_semaphore = VK_NULL_HANDLE;
        }
    }

    for (auto &image_data : per_image_data_) {
        if (VK_NULL_HANDLE != image_data.render_semaphore) {
            vkDestroySemaphore(context_->device(), image_data.render_semaphore, nullptr);
            image_data.render_semaphore = VK_NULL_HANDLE;
        }
    }
}

auto RendererScheduler::resizeSwapchain(const VkExtent2D &surface_extent, const VkExtent2D &framebuffer_extent)
    -> void {
    context_->resize(surface_extent, framebuffer_extent);
    createSwapchainData();
    swapchain_needs_update_ = false;
}

auto RendererScheduler::createSwapchainData() -> void {
    VkSemaphoreCreateInfo semaphore_desc = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };

    const auto num_swapchain_images = context_->swapchainImages().size();

    for (auto &image_data : per_image_data_) {
        if (VK_NULL_HANDLE != image_data.render_semaphore) {
            vkDestroySemaphore(context_->device(), image_data.render_semaphore, nullptr);
            image_data.render_semaphore = VK_NULL_HANDLE;
        }
    }

    per_image_data_.clear();
    per_image_data_.resize(num_swapchain_images);

    for (size_t i = 0; i < num_swapchain_images; ++i) {
        VK_CHECK_ERROR(
            vkCreateSemaphore(context_->device(), &semaphore_desc, nullptr, &per_image_data_[i].render_semaphore));
    }
}

auto RendererScheduler::beginFrame() -> std::optional<FrameContext> {
    auto &current_frame_data = per_frame_data_[current_frame_index_];

    // wait for fence
    VK_CHECK_ERROR(vkWaitForFences(context_->device(), 1, &current_frame_data.fence, VK_TRUE, UINT64_MAX));
    VK_CHECK_ERROR(vkResetFences(context_->device(), 1, &current_frame_data.fence));

    // garbage collect data referenced by the finished frame
    for (auto *resource : current_frame_data.deletion_queue) {
        delete resource;
    }

    current_frame_data.deletion_queue.clear();

    // acquire swapchain image
    uint32_t image_index = 0;
    {
        VkResult res = vkAcquireNextImageKHR(
            context_->device(), context_->swapchain(), UINT64_MAX, current_frame_data.present_semaphore, VK_NULL_HANDLE,
            &image_index);

        switch (res) {
        case VK_SUCCESS:
        case VK_SUBOPTIMAL_KHR:
            break;
        case VK_ERROR_OUT_OF_DATE_KHR:
            LogInfo("vulkan: renderer awaiting resize event");
            swapchain_needs_update_ = true;
            break;
        default:
            LogError("cannot acquire next swapchain image: {}", string_VkResult(res));
            return std::nullopt;
        }
    }

    // begin recording command buffer
    const auto command_buffer = per_frame_data_[current_frame_index_].command_buffer;
    VK_CHECK_ERROR(vkResetCommandBuffer(command_buffer, 0));

    VkCommandBufferBeginInfo command_buffer_begin_desc = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    VK_CHECK_ERROR(vkBeginCommandBuffer(command_buffer, &command_buffer_begin_desc));

    return createFrameContext(current_frame_index_, image_index);
}

auto RendererScheduler::endFrame(const FrameContext &context) -> util::Result {
    const auto command_buffer = per_frame_data_[context.getCurrentFrameIndex()].command_buffer;
    auto &current_frame_data = per_frame_data_[context.getCurrentFrameIndex()];
    auto &current_image_data = per_image_data_[context.getCurrentImageIndex()];

    vkEndCommandBuffer(command_buffer);

    VkPipelineStageFlags wait_stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &current_frame_data.present_semaphore,
        .pWaitDstStageMask = &wait_stages,
        .commandBufferCount = 1,
        .pCommandBuffers = &command_buffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &current_image_data.render_semaphore,
    };

    VK_CHECK_ERROR(vkQueueSubmit(context_->graphicsQueue(), 1, &submit_info, current_frame_data.fence));
    current_frame_index_ = (current_frame_index_ + 1) % kNumFramesInFlight;

    const auto image_index = context.getCurrentImageIndex();
    VkSwapchainKHR swapchain = context_->swapchain();
    VkPresentInfoKHR present_info = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &current_image_data.render_semaphore,
        .swapchainCount = 1,
        .pSwapchains = &swapchain,
        .pImageIndices = &image_index,
    };

    {
        VkResult res = vkQueuePresentKHR(context_->presentQueue(), &present_info);

        switch (res) {
        case VK_SUCCESS:
        case VK_SUBOPTIMAL_KHR:
            break;
        case VK_ERROR_OUT_OF_DATE_KHR:
            LogInfo("vulkan: renderer awaiting resize event");
            swapchain_needs_update_ = true;
            break;
        default:
            LogError("cannot present swapchain image: {}", string_VkResult(res));
            return util::Result::eFailure;
        }
    }

    return util::Result::eSuccess;
}

} // namespace graphics
