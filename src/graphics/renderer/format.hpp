#pragma once
#include <vulkan/vulkan.h>

namespace graphics {

// clang-format off
enum class Format : uint32_t {
    UNKNOWN                = VK_FORMAT_UNDEFINED,
    R8_UINT                = VK_FORMAT_R8_UINT,
    R8_SINT                = VK_FORMAT_R8_SINT,
    R8_UNORM               = VK_FORMAT_R8_UNORM,
    R8_SNORM               = VK_FORMAT_R8_SNORM,
    RG8_UINT               = VK_FORMAT_R8G8_UINT,
    RG8_SINT               = VK_FORMAT_R8G8_SINT,
    RG8_UNORM              = VK_FORMAT_R8G8_UNORM,
    RG8_SNORM              = VK_FORMAT_R8G8_SNORM,
    R16_UINT               = VK_FORMAT_R16_UINT,
    R16_SINT               = VK_FORMAT_R16_SINT,
    R16_UNORM              = VK_FORMAT_R16_UNORM,
    R16_SNORM              = VK_FORMAT_R16_SNORM,
    R16_FLOAT              = VK_FORMAT_R16_SFLOAT,
    B5G6R5_UNORM           = VK_FORMAT_B5G6R5_UNORM_PACK16,
    B5G5R5A1_UNORM         = VK_FORMAT_B5G5R5A1_UNORM_PACK16,
    RGBA8_UINT             = VK_FORMAT_R8G8B8A8_UINT,
    RGBA8_SINT             = VK_FORMAT_R8G8B8A8_SINT,
    RGBA8_UNORM            = VK_FORMAT_R8G8B8A8_UNORM,
    RGBA8_SNORM            = VK_FORMAT_R8G8B8A8_SNORM,
    BGRA8_UNORM            = VK_FORMAT_B8G8R8A8_UNORM,
    SRGBA8_UNORM           = VK_FORMAT_R8G8B8A8_SRGB,
    SBGRA8_UNORM           = VK_FORMAT_B8G8R8A8_SRGB,
    R10G10B10A2_UNORM      = VK_FORMAT_A2B10G10R10_UNORM_PACK32,
    R11G11B10_FLOAT        = VK_FORMAT_B10G11R11_UFLOAT_PACK32,
    RG16_UINT              = VK_FORMAT_R16G16_UINT,
    RG16_SINT              = VK_FORMAT_R16G16_SINT,
    RG16_UNORM             = VK_FORMAT_R16G16_UNORM,
    RG16_SNORM             = VK_FORMAT_R16G16_SNORM,
    RG16_FLOAT             = VK_FORMAT_R16G16_SFLOAT,
    R32_UINT               = VK_FORMAT_R32_UINT,
    R32_SINT               = VK_FORMAT_R32_SINT,
    R32_FLOAT              = VK_FORMAT_R32_SFLOAT,
    RGBA16_UINT            = VK_FORMAT_R16G16B16A16_UINT,
    RGBA16_SINT            = VK_FORMAT_R16G16B16A16_SINT,
    RGBA16_FLOAT           = VK_FORMAT_R16G16B16A16_SFLOAT,
    RGBA16_UNORM           = VK_FORMAT_R16G16B16A16_UNORM,
    RGBA16_SNORM           = VK_FORMAT_R16G16B16A16_SNORM,
    RG32_UINT              = VK_FORMAT_R32G32_UINT,
    RG32_SINT              = VK_FORMAT_R32G32_SINT,
    RG32_FLOAT             = VK_FORMAT_R32G32_SFLOAT,
    RGB32_UINT             = VK_FORMAT_R32G32B32_UINT,
    RGB32_SINT             = VK_FORMAT_R32G32B32_SINT,
    RGB32_FLOAT            = VK_FORMAT_R32G32B32_SFLOAT,
    RGBA32_UINT            = VK_FORMAT_R32G32B32A32_UINT,
    RGBA32_SINT            = VK_FORMAT_R32G32B32A32_SINT,
    RGBA32_FLOAT           = VK_FORMAT_R32G32B32A32_SFLOAT,
    D16                    = VK_FORMAT_D16_UNORM,
    D24S8                  = VK_FORMAT_D24_UNORM_S8_UINT,
    X24G8_UINT             = VK_FORMAT_D24_UNORM_S8_UINT,
    D32                    = VK_FORMAT_D32_SFLOAT,
    D32S8                  = VK_FORMAT_D32_SFLOAT_S8_UINT,
    X32G8_UINT             = VK_FORMAT_D32_SFLOAT_S8_UINT,
    BC1_UNORM              = VK_FORMAT_BC1_RGBA_UNORM_BLOCK,
    BC1_UNORM_SRGB         = VK_FORMAT_BC1_RGBA_SRGB_BLOCK,
    BC2_UNORM              = VK_FORMAT_BC2_UNORM_BLOCK,
    BC2_UNORM_SRGB         = VK_FORMAT_BC2_SRGB_BLOCK,
    BC3_UNORM              = VK_FORMAT_BC3_UNORM_BLOCK,
    BC3_UNORM_SRGB         = VK_FORMAT_BC3_SRGB_BLOCK,
    BC4_UNORM              = VK_FORMAT_BC4_UNORM_BLOCK,
    BC4_SNORM              = VK_FORMAT_BC4_SNORM_BLOCK,
    BC5_UNORM              = VK_FORMAT_BC5_UNORM_BLOCK,
    BC5_SNORM              = VK_FORMAT_BC5_SNORM_BLOCK,
    BC6H_UFLOAT            = VK_FORMAT_BC6H_UFLOAT_BLOCK,
    BC6H_SFLOAT            = VK_FORMAT_BC6H_SFLOAT_BLOCK,
    BC7_UNORM              = VK_FORMAT_BC7_UNORM_BLOCK,
    BC7_UNORM_SRGB         = VK_FORMAT_BC7_SRGB_BLOCK,
};
// clang-format on

inline auto formatToVk(Format format) -> VkFormat { return static_cast<VkFormat>(format); }

inline auto isDepthFormat(const Format &format) -> bool {
    switch (format) {
    case Format::D16:
    case Format::D24S8:
    case Format::D32:
    case Format::D32S8:
        return true;
    default:
        return false;
    }
}

} // namespace graphics