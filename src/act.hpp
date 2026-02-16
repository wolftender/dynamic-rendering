#pragma once
#include <concepts>
#include <optional>
#include <span>
#include <variant>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "util.hpp"

namespace act {

using namespace util::bytes;

struct VertexStatic {
    glm::fvec3 position;
    glm::fvec3 normal;
    glm::fvec2 texcoord;
    glm::fvec3 color;
    glm::fvec4 tangent;
};

struct VertexRigged {
    glm::fvec3 position;
    glm::fvec3 normal;
    glm::fvec2 texcoord;
    glm::fvec3 color;
    glm::fvec4 tangent;
    glm::uvec4 joints;
    glm::fvec4 weights;
};

template <typename T>
concept vertex_type = std::convertible_to<T, VertexStatic> || std::convertible_to<T, VertexRigged>;

template <typename T>
concept vertex_range = std::ranges::range<T> && vertex_type<std::ranges::range_value_t<T>>;

constexpr u32 kMagicNumber = 0x52544341;
constexpr u32 kWriterVersion = 0x10000000;
constexpr u32 kWriterSoftware = 0xdeadbeef;

// clang-format off
enum class ImageMimeType : u32 {
    eRawBitmap = 0x10000001,
    ePngBitmap = 0x10000002,
    eJpgBitmap = 0x10000003,
    eTgaBitmap = 0x10000004
};
// clang-format on

// clang-format off
enum class TextureWrapType : u32 {
    eRepeat         = 0x10010001,
    eClampToEdge    = 0x10010002,
    eMirroredRepeat = 0x10010003,
};
// clang-format on

// clang-format off
enum class SubmeshVertexLayout : u32 {
    eVertexStatic   = 0x10020001,
    eVertexRigged   = 0x10020002,
};
// clang-format on

// clang-format off
enum class AnimationInterpolationMode : u32 {
    eLinear         = 0x10030001,
    eCubicSpline    = 0x10030002,
    eStep           = 0x10030003,
};
// clang-format on

// clang-format off
enum class AnimationPropertyType : u32 {
    eTranslation    = 0x10040001,
    eRotation       = 0x10040002,
    eScale          = 0x10040003,
};
// clang-format on

// clang-format off
enum class BlockType : u32 {
    eImageBlock             = 0x20000001,
    eTextureBlock           = 0x20000002,
    eMaterialBlock          = 0x20000003,
    eNodeBlock              = 0x20000004,
    eMeshBlock              = 0x20000005,
    eSubmeshBlock           = 0x20000006,
    eSkinBlock              = 0x20000007,
    eSkinNodeBlock          = 0x20000008,
    eAnimationBlock         = 0x20000009,
    eAnimChannelBlock       = 0x2000000a,
};
// clang-format on

// clang-format off
enum class CommandType : u32 {
    eImageSetMimeType           = 0x30010001,   // [uint32]
    eImageSetDimensions         = 0x30010002,   // [uint32][uint32]
    eImageSetBuffer             = 0x30010003,   // [buffer]

    eTextureSetImage            = 0x30020001,   // [uint32]
    eTextureSetWrapS            = 0x30020002,   // [uint32]
    eTextureSetWrapT            = 0x30020003,   // [uint32]

    eMaterialSetBaseColor       = 0x30030001,   // [float32][float32][float32][float32]
    eMaterialSetRoughness       = 0x30030002,   // [float32]
    eMaterialSetMetallic        = 0x30030003,   // [float32]
    eMaterialSetAlbedoMap       = 0x30030004,   // [uint32]
    eMaterialSetNormalMap       = 0x30030005,   // [uint32]

    eNodeSetTranslation         = 0x30040001,   // [float32][float32][float32]
    eNodeSetRotation            = 0x30040002,   // [float32][float32][float32][float32]
    eNodeSetScale               = 0x30040003,   // [float32][float32][float32]
    eNodeSetMesh                = 0x30040004,   // [uint32]
    eNodeSetSkin                = 0x30040005,   // [uint32]
    eNodeSetParent              = 0x30040006,   // [uint32]

    eMeshAddSubmesh             = 0x30050001,   // [uint32]

    eSubmeshSetLayout           = 0x30060001,   // [uint32]
    eSubmeshSetVertices         = 0x30060002,   // [buffer]
    eSubmeshSetIndices          = 0x30060003,   // [buffer]
    eSubmeshSetMaterial         = 0x30060004,   // [uint32]

    eSkinAddNode                = 0x30070001,   // [uint32]

    eSkinNodeSetNode            = 0x30080001,   // [uint32]
    eSkinNodeSetMatrix          = 0x30080002,   // [buffer]

    eAnimationAddChannel        = 0x30090001,   // [uint32]

    eAnimChannelSetNode         = 0x300a0001,   // [uint32]
    eAnimChannelSetProp         = 0x300a0002,   // [uint32]
    eAnimChannelSetMode         = 0x300a0003,   // [uint32]
    eAnimChannelSetTimeline     = 0x300a0004,   // [buffer]
    eAnimChannelSetKeyframes    = 0x300a0005    // [buffer] 
};
// clang-format on

template <AnimationPropertyType> struct AnimationPropertyTraits;

template <> struct AnimationPropertyTraits<AnimationPropertyType::eTranslation> {
    using DataType = glm::fvec3;
};

template <> struct AnimationPropertyTraits<AnimationPropertyType::eRotation> {
    using DataType = glm::fquat;
};

template <> struct AnimationPropertyTraits<AnimationPropertyType::eScale> {
    using DataType = glm::fvec3;
};

struct Model final {
    struct Image final {
        ImageMimeType mime_type;
        glm::uvec2 dimensions;
        std::vector<u8> buffer;
    };

    struct Texture final {
        u32 image_id;

        TextureWrapType wrap_s;
        TextureWrapType wrap_t;
    };

    struct Material final {
        std::optional<u32> albedo_map_id;
        std::optional<u32> normal_map_id;

        glm::fvec4 base_color;
        f32 roughness;
        f32 metallic;
    };

    struct Node final {
        glm::fvec3 translation;
        glm::fvec3 scale;
        glm::fquat rotation;

        std::optional<u32> mesh_id;
        std::optional<u32> skin_id;
        std::optional<u32> parent_id;
    };

    struct Mesh final {
        std::vector<u32> submesh_ids;
    };

    template <vertex_type V> struct SubMesh final {
        SubmeshVertexLayout layout;
        std::vector<V> vertices;
        std::vector<u32> indices;
        u32 material;
    };

    struct Skin final {
        std::vector<u32> skin_node_ids;
    };

    struct SkinNode final {
        u32 node_id;
        glm::fmat4x4 inverse_bind_matrix;
    };

    struct Animation final {
        std::vector<u32> channel_ids;
    };

    template <AnimationPropertyType Property> struct AnimationChannel final {
        using Traits = AnimationPropertyTraits<Property>;

        u32 node_id;
        AnimationInterpolationMode interpolation;

        struct Keyframe {
            float time;
            Traits::DataType value;
        };

        std::vector<Keyframe> keyframes;
    };

    using StaticSubmesh = SubMesh<VertexStatic>;
    using RiggedSubmesh = SubMesh<VertexRigged>;

    using AnySubmesh = std::variant<StaticSubmesh, RiggedSubmesh>;

    using RotationAnimationChannel = AnimationChannel<AnimationPropertyType::eRotation>;
    using ScaleAnimationChannel = AnimationChannel<AnimationPropertyType::eScale>;
    using TranslationAnimationChannel = AnimationChannel<AnimationPropertyType::eTranslation>;

    using AnyAnimationChannel =
        std::variant<RotationAnimationChannel, ScaleAnimationChannel, TranslationAnimationChannel>;

    std::vector<Image> images;
    std::vector<Texture> textures;
    std::vector<Material> materials;
    std::vector<Node> nodes;
    std::vector<Mesh> meshes;
    std::vector<AnySubmesh> submeshes;
    std::vector<Skin> skins;
    std::vector<SkinNode> skin_nodes;
    std::vector<Animation> animations;
    std::vector<AnyAnimationChannel> animation_channels;

    static auto loadFromBinary(std::span<const u8> binary) -> std::optional<Model>;
};

} // namespace act
