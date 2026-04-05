#pragma once
#include <array>
#include <vector>

#include "graphics/vulkan.hpp"
#include "graphics/renderer/resource.hpp"
#include "graphics/renderer/buffer.hpp"
#include "graphics/renderer/texture.hpp"
#include "graphics/renderer/descriptor.hpp"

namespace graphics {

class RendererScheduler final : public IResourceScheduler {
public:
    static constexpr uint32_t kNumFramesInFlight = 2;

    template <typename T, uint32_t kNumCopies = kNumFramesInFlight>
        requires std::is_base_of_v<RendererResource, T>
    class MutableResource final {
    public:
        static auto create(IResourceScheduler *scheduler, const T::Description &description)
            -> std::unique_ptr<MutableResource> {
            std::unique_ptr<MutableResource> resource{new (std::nothrow) MutableResource(scheduler)};
            if (!resource) {
                LogError("vulkan: renderer wrtiable buffer creation failed");
                return nullptr;
            }

            for (uint32_t i = 0; i < kNumCopies; ++i) {
                resource->gpu_buffers_[i] = T::create(scheduler, description);
            }

            return resource;
        }

        MutableResource(const MutableResource &) = delete;
        auto operator=(const MutableResource &) = delete;

        MutableResource(MutableResource &&) = delete;
        auto operator=(MutableResource &&) = delete;

        auto getCurrent() -> T * {
            const auto current_frame = scheduler_->currentFrameIndex();
            if (current_frame >= kNumCopies) {
                return nullptr;
            }

            return gpu_buffers_[current_frame];
        }

        auto getCurrent() const -> const T * {
            const auto current_frame = scheduler_->currentFrameIndex();
            if (current_frame >= kNumCopies) {
                return nullptr;
            }

            return gpu_buffers_[current_frame];
        }

    private:
        MutableResource(IResourceScheduler *scheduler) : scheduler_{scheduler} {}

        IResourceScheduler *scheduler_ = nullptr;
        std::array<util::RefCountedPtr<T>, kNumCopies> gpu_buffers_;
    };

    using MutableBuffer = MutableResource<RendererBuffer>;
    using MutableTexture = MutableResource<RendererTexture>;
    using DescriptorSetArray = DescriptorSetArray<kNumFramesInFlight>;

    struct PerFrameData final {
        VkCommandBuffer command_buffer = VK_NULL_HANDLE;

        VkFence fence = VK_NULL_HANDLE;
        VkSemaphore present_semaphore = VK_NULL_HANDLE;

        std::vector<RendererResource *> deletion_queue;
    };

    static auto create(Context *context) -> std::unique_ptr<RendererScheduler>;

    // IResourceScheduler
    auto context() -> Context * override { return context_; }
    auto context() const -> const Context * override { return context_; }
    auto currentFrameIndex() const -> uint32_t override { return current_frame_index_; }

    auto swapchainOutOfDate() const -> bool { return swapchain_needs_update_; }
    auto resizeSwapchain(const VkExtent2D &surface_extent, const VkExtent2D &framebuffer_extent) -> void;

    auto commandBufffer(const FrameContext &context) -> VkCommandBuffer {
        return per_frame_data_[context.getCurrentFrameIndex()].command_buffer;
    }

    ~RendererScheduler() noexcept;

    RendererScheduler(const RendererScheduler &) = delete;
    auto operator=(const RendererScheduler &) = delete;

    RendererScheduler(RendererScheduler &&) = delete;
    auto operator=(RendererScheduler &&) = delete;

    template <std::invocable<const FrameContext &> F> auto frame(F handler) -> util::Result {
        const auto frame_context = beginFrame();
        if (!frame_context.has_value()) {
            return util::Result::eFailure;
        }

        handler(frame_context.value());
        return endFrame(frame_context.value());
    }

    template <typename T>
        requires std::is_base_of_v<RendererResource, T>
    auto use(const FrameContext &context, T *resource) -> T & {
        resource->addFrameReference(context);
        return *resource;
    }

    template <typename T>
        requires std::is_base_of_v<RendererResource, T>
    auto use(const FrameContext &context, util::RefCountedPtr<T> resource) -> T & {
        resource->addFrameReference(context);
        return *resource;
    }

private:
    auto createSwapchainData() -> void;

    auto beginFrame() -> std::optional<FrameContext>;
    auto endFrame(const FrameContext &context) -> util::Result;

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

template <typename cbBufferDataType> class MutableSharedBuffer final {
public:
    static auto create(IResourceScheduler *scheduler) -> MutableSharedBuffer {
        const RendererBuffer::Description description = {
            .memory = RendererBuffer::MemoryType::eDevice,
            .size = sizeof(cbBufferDataType),
        };

        MutableSharedBuffer buffer = {};
        buffer.buffer_ = RendererScheduler::MutableBuffer::create(scheduler, description);

        return buffer;
    }

    MutableSharedBuffer() : buffer_{nullptr} {}

    MutableSharedBuffer(const MutableSharedBuffer &) = delete;
    auto operator=(const MutableSharedBuffer &) = delete;

    MutableSharedBuffer(MutableSharedBuffer &&) noexcept = default;
    auto operator=(MutableSharedBuffer &&) noexcept -> MutableSharedBuffer & = default;

    operator bool() const { return valid(); }
    auto valid() const -> bool { return nullptr != buffer_; }

    auto data() -> cbBufferDataType & { return data_; }
    auto data() const -> const cbBufferDataType & { return data_; }

    auto description() const -> const RendererBuffer::Description & { return buffer_->getCurrent()->description(); }

    auto buffer() -> RendererBuffer * { return buffer_->getCurrent(); }
    auto buffer() const -> const RendererBuffer * { return buffer_->getCurrent(); }

    auto nativeBuffer() const -> VkBuffer { return buffer_->getCurrent()->nativeBuffer(); }
    auto deviceAddress() const -> VkDeviceAddress { return buffer_->getCurrent()->deviceAddress(); }
    auto cpuMappedAddress() const -> void * { return buffer_->getCurrent()->cpuMappedAddress(); }
    auto upload() -> void { std::memcpy(&data_, cpuMappedAddress(), sizeof(cbBufferDataType)); }

private:
    cbBufferDataType data_ = {};
    std::unique_ptr<RendererScheduler::MutableBuffer> buffer_ = {};
};

} // namespace graphics
