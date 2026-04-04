#pragma once
#include <array>
#include <vector>

#include "graphics/vulkan.hpp"
#include "graphics/renderer/resource.hpp"
#include "graphics/renderer/texture.hpp"
#include "graphics/renderer/descriptor.hpp"

namespace graphics {

class RendererScheduler final : public IResourceScheduler {
public:
    static constexpr uint32_t kNumFramesInFlight = 2;

    class WritableSharedBuffer final {};
    class WritableStagedBuffer final {};
    class MutableHostBuffer final {};
    class MutableHostTexture final {};

    using DescriptorSetArray = DescriptorSetArray<kNumFramesInFlight>;

    struct PerFrameData final {
        VkCommandBuffer command_buffer = VK_NULL_HANDLE;

        VkFence fence = VK_NULL_HANDLE;
        VkSemaphore present_semaphore = VK_NULL_HANDLE;

        std::vector<RendererResource> deletion_queue;
    };

    static auto create(Context *context) -> std::unique_ptr<RendererScheduler>;

    // IResourceScheduler
    auto context() -> Context * override { return context_; }
    auto context() const -> const Context * override { return context_; }
    auto currentFrameIndex() const -> uint32_t override { return current_frame_index_; }

    auto swapchainOutOfDate() const -> bool { return swapchain_needs_update_; }
    auto resizeSwapchain(const VkExtent2D &surface_extend, const VkExtent2D &framebuffer_extent);

    ~RendererScheduler() noexcept;

    RendererScheduler(const RendererScheduler &) = delete;
    auto operator=(const RendererScheduler &) = delete;

    RendererScheduler(RendererScheduler &&) = delete;
    auto operator=(RendererScheduler &&) = delete;

    template <std::invocable F> auto frame(F handler) -> void {
        beginFrame();
        handler();
        endFrame();
    }

private:
    auto createSwapchainData() -> void;

    auto beginFrame() -> util::Result;
    auto endFrame() -> util::Result;

    RendererScheduler() = default;

    struct PerSwapchainImageData final {
        VkSemaphore render_semaphore = VK_NULL_HANDLE;
    };

    Context *context_ = nullptr;
    VkCommandPool command_pool_ = VK_NULL_HANDLE;

    uint32_t current_frame_index_ = 0;
    bool swapchain_needs_update_ = false;

    std::array<PerFrameData, kNumFramesInFlight> per_frame_data_;
    std::vector<PerSwapchainImageData> per_image_data_;
};

} // namespace graphics
