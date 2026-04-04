#pragma once
#include "common/refcounted.hpp"

#include "graphics/memory.hpp"
#include "graphics/renderer/format.hpp"
#include "graphics/renderer/resource.hpp"
#include "graphics/renderer/scheduler.hpp"

namespace graphics {

class RendererScheduler;

class RendererTexture final : public RendererResource {
public:
    enum Usage {
        eUsageNone = 0,
        eUsageShaderSample = 1 << 0,
        eUsageColorAttachment = 1 << 1,
        eUsageDepthAttachment = 1 << 2,
        eUsageTransferSrc = 1 << 3,
        eUsageTransferDst = 1 << 4,
    };

    enum class SampleCount {
        eSampleCount1 = 1,
        eSampleCount2 = 2,
        eSampleCount4 = 4,
        eSampleCount8 = 8,
        eSampleCount16 = 16,
    };

    enum class MagFilter {
        eNearest,
        eLinear,
    };

    enum class MinFilter {
        eNearest,
        eLinear,
    };

    enum class TextureType {
        eTexture1D,
        eTexture2D,
        eTexture3D,
        eTextureCubemap,
        eTexture1DArray,
        eTexture2DArray,
        eTextureCubemapArray,
    };

    struct Description {
        TextureType type;

        uint32_t width = 1;
        uint32_t height = 1;
        uint32_t depth = 1;
        uint32_t array_size = 1;

        MinFilter min_filter = MinFilter::eNearest;
        MagFilter mag_filter = MagFilter::eNearest;

        Usage usage = Usage::eUsageNone;
        SampleCount sample_count = SampleCount::eSampleCount1;
        Format format = Format::UNKNOWN;
    };

    struct RgbaDescription {
        uint32_t width = 1;
        uint32_t height = 1;

        MinFilter min_filter = MinFilter::eNearest;
        MagFilter mag_filter = MagFilter::eNearest;

        Usage usage = Usage::eUsageNone;
        std::span<uint8_t> init_data;
    };

    static auto create(RendererScheduler *scheduler, const Description &desc) -> util::RefCountedPtr<RendererTexture>;
    static auto createFromRgba(RendererScheduler *scheduler, const RgbaDescription &desc)
        -> util::RefCountedPtr<RendererTexture>;

    ~RendererTexture() noexcept;

    RendererTexture(const RendererTexture &) = delete;
    auto operator=(const RendererTexture &) = delete;

    RendererTexture(RendererTexture &&) noexcept = delete;
    auto operator=(RendererTexture &&) noexcept = delete;

    auto description() const -> const Description & { return description_; }

    auto image() const -> const Image & { return image_; }
    auto view() const -> const Image::View & { return view_; }

    auto nativeImage() const -> VkImage { return image_.image(); }
    auto nativeView() const -> VkImageView { return view_.view(); }
    auto nativeSampler() const -> VkSampler { return sampler_; }

private:
    RendererTexture(
        RendererScheduler *scheduler, Image &&image, Image::View &&view, VkSampler sampler, Description description);

    Context *context_ = nullptr;
    Image image_ = {};
    Image::View view_ = {};
    VkSampler sampler_ = VK_NULL_HANDLE;
    Description description_ = {};
};

auto operator|(const RendererTexture::Usage &u1, const RendererTexture::Usage &u2) -> RendererTexture::Usage;
auto operator&(const RendererTexture::Usage &u1, const RendererTexture::Usage &u2) -> RendererTexture::Usage;
auto operator~(const RendererTexture::Usage &u) -> RendererTexture::Usage;

} // namespace graphics
