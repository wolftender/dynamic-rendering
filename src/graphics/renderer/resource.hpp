#pragma once
#include <atomic>
#include <optional>

#include "graphics/vulkan.hpp"

namespace graphics {

class RendererResource;

class IResourceScheduler {
public:
    virtual ~IResourceScheduler() = default;
    virtual auto safeDelete(const RendererResource *) -> void;

    virtual auto context() const -> const Context * = 0;
    virtual auto context() -> Context * = 0;
    virtual auto currentFrameIndex() const -> uint32_t = 0;
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

private:
    IResourceScheduler *scheduler_ = nullptr;

    std::atomic<uint32_t> ref_count_ = 1;
    std::optional<uint32_t> last_scheduled_frame_ = std::nullopt;
};

} // namespace graphics
