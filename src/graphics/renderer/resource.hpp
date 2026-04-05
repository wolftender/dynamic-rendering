#pragma once
#include <atomic>
#include <optional>

#include "graphics/vulkan.hpp"

namespace graphics {

class RendererResource;

class IResourceScheduler {
public:
    struct FrameContext final {
    public:
        FrameContext(const FrameContext &) = delete;
        auto operator=(const FrameContext &) = delete;

        FrameContext(FrameContext &&) noexcept = default;
        auto operator=(FrameContext &&) noexcept -> FrameContext & = default;

        auto getCurrentFrameIndex() const -> uint32_t { return current_frame_index_; }
        auto getCurrentImageIndex() const -> uint32_t { return current_image_index_; }

    private:
        FrameContext(uint32_t current_frame_index, uint32_t current_image_index)
            : current_frame_index_{current_frame_index}, current_image_index_{current_image_index} {}

        uint32_t current_frame_index_ = 0;
        uint32_t current_image_index_ = 0;

        friend class IResourceScheduler;
    };

    virtual ~IResourceScheduler() = default;
    virtual auto safeDelete(const RendererResource *) -> void;

    virtual auto context() const -> const Context * = 0;
    virtual auto context() -> Context * = 0;
    virtual auto currentFrameIndex() const -> uint32_t = 0;

protected:
    auto createFrameContext(uint32_t frame_index, uint32_t image_index) -> FrameContext {
        return FrameContext{frame_index, image_index};
    }
};

class RendererResource {
public:
    RendererResource(IResourceScheduler *scheduler) : scheduler_{scheduler}, ref_count_{1} {}
    virtual ~RendererResource() = default;

    RendererResource(const RendererResource &) = delete;
    auto operator=(const RendererResource &) = delete;

    RendererResource(RendererResource &&) noexcept = delete;
    auto operator=(RendererResource &&) noexcept = delete;

    auto scheduler() -> IResourceScheduler * { return scheduler_; }
    auto scheduler() const -> const IResourceScheduler * { return scheduler_; }

    auto getRefCount() const -> uint32_t { return ref_count_; }
    auto addRef() -> uint32_t { return ++ref_count_; }

    auto release() -> uint32_t {
        auto result = --ref_count_;
        if (result == 0) {
            scheduler_->safeDelete(this);
        }

        return result;
    }

    auto getLastScheduledFrame() const -> std::optional<uint32_t> { return last_scheduled_frame_; }

    auto addFrameReference(const IResourceScheduler::FrameContext &context) -> void {
        last_scheduled_frame_ = context.getCurrentFrameIndex();
    }

private:
    IResourceScheduler *scheduler_ = nullptr;

    std::atomic<uint32_t> ref_count_ = 1;
    std::optional<uint32_t> last_scheduled_frame_ = std::nullopt;

    friend class IResourceScheduler;
};

} // namespace graphics
