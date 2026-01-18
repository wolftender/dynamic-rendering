#include <cstdint>
#include <concepts>
#include <optional>
#include <span>
#include <variant>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace act {

// clang-format off
using u8    = uint8_t;
using u16   = uint16_t;
using u32   = uint32_t;
using u64   = uint64_t;
using s8    = int8_t;
using s16   = int16_t;
using s32   = int32_t;
using s64   = int64_t;
using f32   = float;
using f64   = double;
// clang-format on

template <typename T>
concept IsPrimitiveType = std::same_as<T, u8> || std::same_as<T, u16> || std::same_as<T, u32> || std::same_as<T, u64> ||
                          std::same_as<T, s8> || std::same_as<T, s16> || std::same_as<T, s32> || std::same_as<T, s64> ||
                          std::same_as<T, f32> || std::same_as<T, f64>;

class BinaryReader final {
public:
    enum class Result { eSuccess, eFailure };

    explicit BinaryReader(std::span<const u8> input_range) : data_span_{std::move(input_range)}, ptr_{0ull} {}
    ~BinaryReader() = default;

    BinaryReader(const BinaryReader &) = delete;
    auto operator=(const BinaryReader &) = delete;

    BinaryReader(BinaryReader &&) noexcept = default;
    auto operator=(BinaryReader &&) noexcept -> BinaryReader & = default;

    auto position() const -> u64 { return ptr_; }
    auto remaining() const -> u64;
    auto seek(u64 location) -> Result;
    auto readBuffer(u64 num_bytes) -> std::optional<std::span<const u8>>;

    template <IsPrimitiveType T> auto read() -> std::optional<T> {
        constexpr auto type_size = sizeof(T);
        const auto end_ptr = data_span_.size();

        if (ptr_ + type_size > end_ptr) {
            return std::nullopt;
        }

        // little endian data read
        T result = *(reinterpret_cast<const T *>(data_span_.data() + ptr_));

        ptr_ = ptr_ + type_size;
        return result;
    }

    template <typename T = f32> auto readVec2() -> std::optional<glm::vec<2, T>> {
        const auto x = read<f32>();
        const auto y = read<f32>();

        if (!x.has_value() || !y.has_value()) {
            return std::nullopt;
        }

        return glm::vec<2, T>{*x, *y};
    }

    template <typename T = f32> auto readVec3() -> std::optional<glm::vec<3, T>> {
        const auto x = read<f32>();
        const auto y = read<f32>();
        const auto z = read<f32>();

        if (!x.has_value() || !y.has_value() || !z.has_value()) {
            return std::nullopt;
        }

        return glm::vec<3, T>{*x, *y, *z};
    }

    template <typename T = f32> auto readVec4() -> std::optional<glm::vec<4, T>> {
        const auto x = read<f32>();
        const auto y = read<f32>();
        const auto z = read<f32>();
        const auto w = read<f32>();

        if (!x.has_value() || !y.has_value() || !z.has_value() || !w.has_value()) {
            return std::nullopt;
        }

        return glm::vec<4, T>{*x, *y, *z, *w};
    }

    template <typename T = f32> auto readQuat() -> std::optional<glm::qua<T>> {
        const auto x = read<f32>();
        const auto y = read<f32>();
        const auto z = read<f32>();
        const auto w = read<f32>();

        if (!x.has_value() || !y.has_value() || !z.has_value() || !w.has_value()) {
            return std::nullopt;
        }

        return glm::qua<T>{*x, *y, *z, *w};
    }

private:
    std::span<const u8> data_span_;
    u64 ptr_;
};

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
