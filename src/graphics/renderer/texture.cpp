#include <volk.h>

#include "graphics/common.hpp"
#include "graphics/renderer/texture.hpp"

namespace graphics {

inline auto minFilterToVk(RendererTexture::MinFilter min_filter) -> VkFilter {
    switch (min_filter) {
    case RendererTexture::MinFilter::eLinear:
        return VK_FILTER_LINEAR;
    case RendererTexture::MinFilter::eNearest:
        return VK_FILTER_NEAREST;
    default:
        return VK_FILTER_LINEAR;
    }
}

inline auto magFilterToVk(RendererTexture::MagFilter mag_filter) -> VkFilter {
    switch (mag_filter) {
    case RendererTexture::MagFilter::eLinear:
        return VK_FILTER_LINEAR;
    case RendererTexture::MagFilter::eNearest:
        return VK_FILTER_NEAREST;
    default:
        return VK_FILTER_LINEAR;
    }
}

inline auto textureTypeToImageTypeVk(RendererTexture::TextureType type) -> VkImageType {
    switch (type) {
    case RendererTexture::TextureType::eTexture1D:
    case RendererTexture::TextureType::eTexture1DArray:
        return VK_IMAGE_TYPE_1D;
    case RendererTexture::TextureType::eTexture2D:
    case RendererTexture::TextureType::eTexture2DArray:
        return VK_IMAGE_TYPE_2D;
    case RendererTexture::TextureType::eTexture3D:
        return VK_IMAGE_TYPE_3D;
    case RendererTexture::TextureType::eTextureCubemap:
    case RendererTexture::TextureType::eTextureCubemapArray:
        return VK_IMAGE_TYPE_2D;
    default:
        return VK_IMAGE_TYPE_MAX_ENUM;
    }
}

inline auto textureTypeToViewTypeVk(RendererTexture::TextureType type) -> VkImageViewType {
    switch (type) {
    case RendererTexture::TextureType::eTexture1D:
        return VK_IMAGE_VIEW_TYPE_1D;
    case RendererTexture::TextureType::eTexture1DArray:
        return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
    case RendererTexture::TextureType::eTexture2D:
        return VK_IMAGE_VIEW_TYPE_2D;
    case RendererTexture::TextureType::eTexture2DArray:
        return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    case RendererTexture::TextureType::eTexture3D:
        return VK_IMAGE_VIEW_TYPE_3D;
    case RendererTexture::TextureType::eTextureCubemap:
        return VK_IMAGE_VIEW_TYPE_CUBE;
    case RendererTexture::TextureType::eTextureCubemapArray:
        return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    default:
        return VK_IMAGE_VIEW_TYPE_MAX_ENUM;
    }
}

inline auto usageToVk(const RendererTexture::Usage &usage) -> VkImageUsageFlags {
    using Usage = RendererTexture::Usage;
    VkImageUsageFlags native_usage = static_cast<VkImageUsageFlagBits>(0);

    if (Usage::eUsageShaderSample == (Usage::eUsageShaderSample & usage)) {
        native_usage = static_cast<VkImageUsageFlagBits>(native_usage | VK_IMAGE_USAGE_SAMPLED_BIT);
    }

    if (Usage::eUsageColorAttachment == (Usage::eUsageColorAttachment & usage)) {
        native_usage = static_cast<VkImageUsageFlagBits>(native_usage | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    }

    if (Usage::eUsageDepthAttachment == (Usage::eUsageDepthAttachment & usage)) {
        native_usage = static_cast<VkImageUsageFlagBits>(native_usage | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
    }

    if (Usage::eUsageTransferSrc == (Usage::eUsageTransferSrc & usage)) {
        native_usage = static_cast<VkImageUsageFlagBits>(native_usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    }

    if (Usage::eUsageTransferDst == (Usage::eUsageTransferDst & usage)) {
        native_usage = static_cast<VkImageUsageFlagBits>(native_usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    }

    return native_usage;
}

RendererTexture::RendererTexture(
    IResourceScheduler *scheduler, Image &&image, Image::View &&view, VkSampler sampler, Description description)
    : RendererResource{scheduler}, image_{std::move(image)}, view_{std::move(view)}, sampler_{sampler},
      description_{std::move(description)} {}

auto RendererTexture::create(IResourceScheduler *scheduler, const Description &description)
    -> util::RefCountedPtr<RendererTexture> {
    const auto type = textureTypeToImageTypeVk(description.type);
    const auto view_type = textureTypeToViewTypeVk(description.type);

    const auto min_filter = minFilterToVk(description.min_filter);
    const auto mag_filter = magFilterToVk(description.mag_filter);
    const auto usage = usageToVk(description.usage);

    VkImageCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = type,
        .format = formatToVk(description.format),
        .extent = {description.width, description.height, description.depth},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = scheduler->context()->chooseBestSampleCount(static_cast<uint32_t>(description.sample_count)),
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
    };

    const auto view_aspect = isDepthFormat(description.format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    auto native_image = scheduler->context()->memory().createImage(create_info);
    auto native_view = native_image.createView(view_type, create_info.format, view_aspect);

    VkSampler native_sampler = VK_NULL_HANDLE;
    VkSamplerCreateInfo sampler_desc = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = mag_filter,
        .minFilter = min_filter,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .anisotropyEnable = VK_TRUE,
        .maxAnisotropy = 8.0f,
        .maxLod = 1,
    };

    VK_CHECK_ERROR(vkCreateSampler(scheduler->context()->device(), &sampler_desc, nullptr, &native_sampler));

    util::RefCountedPtr<RendererTexture> texture{new (std::nothrow) RendererTexture(
        scheduler, std::move(native_image), std::move(native_view), std::move(native_sampler), description)};

    return texture;
}

auto RendererTexture::createFromRgba(IResourceScheduler *scheduler, const RgbaDescription &desc)
    -> util::RefCountedPtr<RendererTexture> {
    Description description = {
        .type = TextureType::eTexture2D,
        .width = desc.width,
        .height = desc.height,
        .depth = 1,
        .array_size = 1,
        .min_filter = desc.min_filter,
        .mag_filter = desc.mag_filter,
        .usage = eUsageShaderSample | eUsageTransferDst,
        .sample_count = SampleCount::eSampleCount1,
        .format = Format::SRGBA8_UNORM,
    };

    const auto usage = usageToVk(description.usage);
    auto native_image = scheduler->context()->memory().createImageRgba(
        usage, VkExtent2D{description.width, description.height}, desc.init_data);
    auto native_view =
        native_image.createView(VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R8G8B8A8_SNORM, VK_IMAGE_ASPECT_COLOR_BIT);

    const auto min_filter = minFilterToVk(description.min_filter);
    const auto mag_filter = magFilterToVk(description.mag_filter);

    VkSampler native_sampler = VK_NULL_HANDLE;
    VkSamplerCreateInfo sampler_desc = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = mag_filter,
        .minFilter = min_filter,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .anisotropyEnable = VK_TRUE,
        .maxAnisotropy = 8.0f,
        .maxLod = 1,
    };

    VK_CHECK_ERROR(vkCreateSampler(scheduler->context()->device(), &sampler_desc, nullptr, &native_sampler));

    util::RefCountedPtr<RendererTexture> texture{new (std::nothrow) RendererTexture(
        scheduler, std::move(native_image), std::move(native_view), std::move(native_sampler), description)};

    return texture;
}

RendererTexture::~RendererTexture() noexcept {
    if (VK_NULL_HANDLE != sampler_) {
        vkDestroySampler(scheduler()->context()->device(), sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }
}

auto operator|(const RendererTexture::Usage &u1, const RendererTexture::Usage &u2) -> RendererTexture::Usage {
    return static_cast<RendererTexture::Usage>(static_cast<uint32_t>(u1) | static_cast<uint32_t>(u2));
}

auto operator&(const RendererTexture::Usage &u1, const RendererTexture::Usage &u2) -> RendererTexture::Usage {
    return static_cast<RendererTexture::Usage>(static_cast<uint32_t>(u1) & static_cast<uint32_t>(u2));
}

auto operator~(const RendererTexture::Usage &u) -> RendererTexture::Usage {
    return static_cast<RendererTexture::Usage>(~static_cast<uint32_t>(u));
}

} // namespace graphics
