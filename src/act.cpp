#include "act.hpp"
#include "logger.hpp"

#include <glm/gtc/type_ptr.hpp>

namespace act {
auto parse_image_mime_type(u32 v) -> std::optional<ImageMimeType> {
    switch (v) {
    case 0x10000001:
        return ImageMimeType::eRawBitmap;
    case 0x10000002:
        return ImageMimeType::ePngBitmap;
    case 0x10000003:
        return ImageMimeType::eJpgBitmap;
    case 0x10000004:
        return ImageMimeType::eTgaBitmap;
    default:
        return std::nullopt;
    }
}

auto parse_texture_wrap_type(u32 v) -> std::optional<TextureWrapType> {
    switch (v) {
    case 0x10010001:
        return TextureWrapType::eRepeat;
    case 0x10010002:
        return TextureWrapType::eClampToEdge;
    case 0x10010003:
        return TextureWrapType::eMirroredRepeat;
    default:
        return std::nullopt;
    }
}

auto parse_submesh_vertex_layout(u32 v) -> std::optional<SubmeshVertexLayout> {
    switch (v) {
    case 0x10020001:
        return SubmeshVertexLayout::eVertexStatic;
    case 0x10020002:
        return SubmeshVertexLayout::eVertexRigged;
    default:
        return std::nullopt;
    }
}

auto parse_animation_interpolation_mode(u32 v) -> std::optional<AnimationInterpolationMode> {
    switch (v) {
    case 0x10030001:
        return AnimationInterpolationMode::eLinear;
    case 0x10030002:
        return AnimationInterpolationMode::eCubicSpline;
    default:
        return std::nullopt;
    }
}

auto parse_animation_property_type(u32 v) -> std::optional<AnimationPropertyType> {
    switch (v) {
    case 0x10040001:
        return AnimationPropertyType::eTranslation;
    case 0x10040002:
        return AnimationPropertyType::eRotation;
    case 0x10040003:
        return AnimationPropertyType::eScale;
    default:
        return std::nullopt;
    }
}

auto parse_block_type(u32 v) -> std::optional<BlockType> {
    switch (v) {
    case 0x20000001:
        return BlockType::eImageBlock;
    case 0x20000002:
        return BlockType::eTextureBlock;
    case 0x20000003:
        return BlockType::eMaterialBlock;
    case 0x20000004:
        return BlockType::eNodeBlock;
    case 0x20000005:
        return BlockType::eMeshBlock;
    case 0x20000006:
        return BlockType::eSubmeshBlock;
    case 0x20000007:
        return BlockType::eSkinBlock;
    case 0x20000008:
        return BlockType::eSkinNodeBlock;
    case 0x20000009:
        return BlockType::eAnimationBlock;
    case 0x2000000a:
        return BlockType::eAnimChannelBlock;
    default:
        return std::nullopt;
    }
}

auto parse_command_type(u32 v) -> std::optional<CommandType> {
    switch (v) {
    case 0x30010001:
        return CommandType::eImageSetMimeType;
    case 0x30010002:
        return CommandType::eImageSetDimensions;
    case 0x30010003:
        return CommandType::eImageSetBuffer;

    case 0x30020001:
        return CommandType::eTextureSetImage;
    case 0x30020002:
        return CommandType::eTextureSetWrapS;
    case 0x30020003:
        return CommandType::eTextureSetWrapT;

    case 0x30030001:
        return CommandType::eMaterialSetBaseColor;
    case 0x30030002:
        return CommandType::eMaterialSetRoughness;
    case 0x30030003:
        return CommandType::eMaterialSetMetallic;
    case 0x30030004:
        return CommandType::eMaterialSetAlbedoMap;
    case 0x30030005:
        return CommandType::eMaterialSetNormalMap;

    case 0x30040001:
        return CommandType::eNodeSetTranslation;
    case 0x30040002:
        return CommandType::eNodeSetRotation;
    case 0x30040003:
        return CommandType::eNodeSetScale;
    case 0x30040004:
        return CommandType::eNodeSetMesh;
    case 0x30040005:
        return CommandType::eNodeSetSkin;

    case 0x30050001:
        return CommandType::eMeshAddSubmesh;

    case 0x30060001:
        return CommandType::eSubmeshSetLayout;
    case 0x30060002:
        return CommandType::eSubmeshSetVertices;
    case 0x30060003:
        return CommandType::eSubmeshSetIndices;

    case 0x30070001:
        return CommandType::eSkinAddNode;

    case 0x30080001:
        return CommandType::eSkinNodeSetNode;
    case 0x30080002:
        return CommandType::eSkinNodeSetMatrix;

    case 0x30090001:
        return CommandType::eAnimationAddChannel;

    case 0x300a0001:
        return CommandType::eAnimChannelSetNode;
    case 0x300a0002:
        return CommandType::eAnimChannelSetProp;
    case 0x300a0003:
        return CommandType::eAnimChannelSetMode;
    case 0x300a0004:
        return CommandType::eAnimChannelSetTimeline;
    case 0x300a0005:
        return CommandType::eAnimChannelSetKeyframes;

    default:
        return std::nullopt;
    }
}

auto BinaryReader::remaining() const -> u64 { return data_span_.size() - ptr_; }

auto BinaryReader::seek(u64 location) -> Result {
    if (location > data_span_.size()) {
        return Result::eFailure;
    }

    ptr_ = location;
    return Result::eSuccess;
}

auto BinaryReader::read_buffer(u64 num_bytes) -> std::optional<std::span<const u8>> {
    const auto end_ptr = data_span_.size();

    if (ptr_ + num_bytes > end_ptr) {
        return std::nullopt;
    }

    const auto data_ptr = data_span_.data() + ptr_;
    ptr_ += num_bytes;

    return std::span<const u8>{data_ptr, num_bytes};
}

#define READ_OR_ERROR(T, reader, output)                                                                               \
    {                                                                                                                  \
        const auto _res = reader.read<T>();                                                                            \
        if (!_res.has_value()) {                                                                                       \
            LogError("act: binary reader failed to get property type={}, name={}", #T, #output);                       \
            return std::nullopt;                                                                                       \
        }                                                                                                              \
        output = _res.value();                                                                                         \
    }

#define READ_ENUM_PROPERTY_OR_ERROR(parser, reader, output)                                                            \
    {                                                                                                                  \
        u32 value;                                                                                                     \
        READ_OR_ERROR(u32, reader, value);                                                                             \
        const auto enum_value = parser(value);                                                                         \
        if (!enum_value.has_value()) {                                                                                 \
            LogError("act: invalid value for {}: {}", #output, value);                                                 \
            return std::nullopt;                                                                                       \
        }                                                                                                              \
        output = enum_value.value();                                                                                   \
    }

#define READ_VEC2_PROPERTY_OR_ERROR(T, reader, output)                                                                 \
    {                                                                                                                  \
        auto value = reader.read_vec2<T>();                                                                            \
        if (!value.has_value()) {                                                                                      \
            LogError("act: binary reader failed to get property vec2 type = {}, name={}", #T, #output);                \
            return std::nullopt;                                                                                       \
        }                                                                                                              \
        output = value.value();                                                                                        \
    }

#define READ_VEC3_PROPERTY_OR_ERROR(T, reader, output)                                                                 \
    {                                                                                                                  \
        auto value = reader.read_vec3<T>();                                                                            \
        if (!value.has_value()) {                                                                                      \
            LogError("act: binary reader failed to get property vec3 type = {}, name={}", #T, #output);                \
            return std::nullopt;                                                                                       \
        }                                                                                                              \
        output = value.value();                                                                                        \
    }

#define READ_VEC4_PROPERTY_OR_ERROR(T, reader, output)                                                                 \
    {                                                                                                                  \
        auto value = reader.read_vec4<T>();                                                                            \
        if (!value.has_value()) {                                                                                      \
            LogError("act: binary reader failed to get property vec4 type = {}, name={}", #T, #output);                \
            return std::nullopt;                                                                                       \
        }                                                                                                              \
        output = value.value();                                                                                        \
    }

#define READ_QUAT_PROPERTY_OR_ERROR(T, reader, output)                                                                 \
    {                                                                                                                  \
        auto value = reader.read_quat<T>();                                                                            \
        if (!value.has_value()) {                                                                                      \
            LogError("act: binary reader failed to get property quat type = {}, name={}", #T, #output);                \
            return std::nullopt;                                                                                       \
        }                                                                                                              \
        output = value.value();                                                                                        \
    }

#define SIZED_PROPERTY_ASSERT(reader, size)                                                                            \
    {                                                                                                                  \
        u32 _size_val;                                                                                                 \
        READ_OR_ERROR(u32, reader, _size_val);                                                                         \
        if (_size_val != size) {                                                                                       \
            LogError("act: size assert failed, {} != {}", _size_val, size);                                            \
            return std::nullopt;                                                                                       \
        }                                                                                                              \
    }

auto read_command_type(BinaryReader &reader) -> std::optional<CommandType> {
    const auto command_type = reader.read<u32>();
    if (!command_type.has_value()) {
        return std::nullopt;
    }

    const auto command_type_enum = parse_command_type(command_type.value());
    return command_type_enum;
}

auto read_buffer(BinaryReader &reader) -> std::optional<std::span<const u8>> {
    u32 buffer_size;
    READ_OR_ERROR(u32, reader, buffer_size);

    return reader.read_buffer(buffer_size);
}

auto parse_image_block(std::span<const u8> block_buffer) -> std::optional<Model::Image> {
    BinaryReader block_reader{block_buffer};
    Model::Image image{};

    while (const auto command_type = read_command_type(block_reader)) {
        switch (*command_type) {
        case act::CommandType::eImageSetMimeType:
            SIZED_PROPERTY_ASSERT(block_reader, sizeof(u32));
            READ_ENUM_PROPERTY_OR_ERROR(parse_image_mime_type, block_reader, image.mime_type);
            break;
        case act::CommandType::eImageSetDimensions:
            SIZED_PROPERTY_ASSERT(block_reader, sizeof(u32) * 2);
            READ_VEC2_PROPERTY_OR_ERROR(u32, block_reader, image.dimensions);
            break;
        case act::CommandType::eImageSetBuffer: {
            const auto buffer = read_buffer(block_reader);
            if (!buffer.has_value()) {
                LogError("act: failed to read image buffer");
                return std::nullopt;
            }

            image.buffer.assign(buffer->begin(), buffer->end());
            break;
        }
        default:
            LogError("act: invalid image command type {}", static_cast<uint32_t>(*command_type));
            return std::nullopt;
        }
    }

    return image;
}

auto parse_texture_block(std::span<const u8> block_buffer) -> std::optional<Model::Texture> {
    BinaryReader block_reader{block_buffer};
    Model::Texture texture = {};

    while (auto command_type = read_command_type(block_reader)) {
        switch (*command_type) {
        case act::CommandType::eTextureSetWrapS:
            SIZED_PROPERTY_ASSERT(block_reader, sizeof(u32));
            READ_ENUM_PROPERTY_OR_ERROR(parse_texture_wrap_type, block_reader, texture.wrap_s);
            break;

        case act::CommandType::eTextureSetWrapT:
            SIZED_PROPERTY_ASSERT(block_reader, sizeof(u32));
            READ_ENUM_PROPERTY_OR_ERROR(parse_texture_wrap_type, block_reader, texture.wrap_t);
            break;

        case act::CommandType::eTextureSetImage:
            SIZED_PROPERTY_ASSERT(block_reader, sizeof(u32));
            READ_OR_ERROR(u32, block_reader, texture.image_id);
            break;

        default:
            LogError("act: invalid texture command type {}", static_cast<uint32_t>(*command_type));
            return std::nullopt;
        }
    }

    return texture;
}

auto parse_material_block(std::span<const u8> block_buffer) -> std::optional<Model::Material> {
    BinaryReader block_reader{block_buffer};
    Model::Material material = {};

    while (auto command_type = read_command_type(block_reader)) {
        switch (*command_type) {
        case act::CommandType::eMaterialSetBaseColor:
            SIZED_PROPERTY_ASSERT(block_reader, sizeof(f32) * 4);
            READ_VEC4_PROPERTY_OR_ERROR(f32, block_reader, material.base_color);
            break;

        case act::CommandType::eMaterialSetMetallic:
            SIZED_PROPERTY_ASSERT(block_reader, sizeof(f32));
            READ_OR_ERROR(f32, block_reader, material.metallic);
            break;

        case act::CommandType::eMaterialSetRoughness:
            SIZED_PROPERTY_ASSERT(block_reader, sizeof(f32));
            READ_OR_ERROR(f32, block_reader, material.roughness);
            break;

        case act::CommandType::eMaterialSetAlbedoMap:
            SIZED_PROPERTY_ASSERT(block_reader, sizeof(u32));
            READ_OR_ERROR(u32, block_reader, material.albedo_map_id);
            break;

        case act::CommandType::eMaterialSetNormalMap:
            SIZED_PROPERTY_ASSERT(block_reader, sizeof(u32));
            READ_OR_ERROR(u32, block_reader, material.normal_map_id);
            break;

        default:
            LogError("act: invalid material command type {}", static_cast<uint32_t>(*command_type));
            return std::nullopt;
        }
    }

    return material;
}

auto parse_node_block(std::span<const u8> block_buffer) -> std::optional<Model::Node> {
    BinaryReader block_reader{block_buffer};
    Model::Node node = {};

    while (auto command_type = read_command_type(block_reader)) {
        switch (*command_type) {
        case act::CommandType::eNodeSetMesh:
            SIZED_PROPERTY_ASSERT(block_reader, sizeof(u32));
            READ_OR_ERROR(u32, block_reader, node.mesh_id);
            break;

        case act::CommandType::eNodeSetRotation:
            SIZED_PROPERTY_ASSERT(block_reader, sizeof(f32) * 4);
            READ_QUAT_PROPERTY_OR_ERROR(f32, block_reader, node.rotation);
            break;

        case act::CommandType::eNodeSetScale:
            SIZED_PROPERTY_ASSERT(block_reader, sizeof(f32) * 3);
            READ_VEC3_PROPERTY_OR_ERROR(f32, block_reader, node.scale);
            break;

        case act::CommandType::eNodeSetTranslation:
            SIZED_PROPERTY_ASSERT(block_reader, sizeof(f32) * 3);
            READ_VEC3_PROPERTY_OR_ERROR(f32, block_reader, node.translation);
            break;

        case act::CommandType::eNodeSetSkin:
            SIZED_PROPERTY_ASSERT(block_reader, sizeof(u32));
            READ_OR_ERROR(u32, block_reader, node.skin_id);
            break;

        case act::CommandType::eNodeSetParent:
            SIZED_PROPERTY_ASSERT(block_reader, sizeof(u32));
            READ_OR_ERROR(u32, block_reader, node.parent_id);
            break;

        default:
            LogError("act: invalid node command type {}", static_cast<uint32_t>(*command_type));
            return std::nullopt;
        }
    }

    return node;
}

auto parse_mesh_block(std::span<const u8> block_buffer) -> std::optional<Model::Mesh> {
    BinaryReader block_reader{block_buffer};
    Model::Mesh mesh = {};

    while (auto command_type = read_command_type(block_reader)) {
        switch (*command_type) {
        case act::CommandType::eMeshAddSubmesh: {
            u32 submesh;
            SIZED_PROPERTY_ASSERT(block_reader, sizeof(u32));
            READ_OR_ERROR(u32, block_reader, submesh);
            mesh.submesh_ids.emplace_back(submesh);

            break;
        }

        default:
            LogError("act: invalid mesh command type {}", static_cast<uint32_t>(*command_type));
            return std::nullopt;
        }
    }

    return mesh;
}

auto parse_submesh_block(std::span<const u8> block_buffer) -> std::optional<Model::AnySubmesh> {
    BinaryReader block_reader{block_buffer};

    std::span<const u8> vertex_buffer;
    std::vector<u32> indices;
    act::SubmeshVertexLayout layout;

    while (auto command_type = read_command_type(block_reader)) {
        switch (*command_type) {
        case act::CommandType::eSubmeshSetLayout:
            SIZED_PROPERTY_ASSERT(block_reader, sizeof(u32));
            READ_ENUM_PROPERTY_OR_ERROR(parse_submesh_vertex_layout, block_reader, layout);
            break;

        case act::CommandType::eSubmeshSetIndices: {
            u32 num_indices;
            READ_OR_ERROR(u32, block_reader, num_indices);

            num_indices = num_indices / sizeof(u32);
            indices.reserve(num_indices);
            u32 index;

            for (auto i = 0u; i < num_indices; ++i) {
                READ_OR_ERROR(u32, block_reader, index);
                indices.emplace_back(index);
            }

            break;
        }

        case act::CommandType::eSubmeshSetVertices: {
            const auto buffer = read_buffer(block_reader);
            if (!buffer.has_value()) {
                LogError("act: failed to read vertex buffer");
                return std::nullopt;
            }

            vertex_buffer = buffer.value();
            break;
        }

        default:
            LogError("act: invalid mesh command type {}", static_cast<uint32_t>(*command_type));
            return std::nullopt;
        }
    }

    // we already know the vertex type so we can parse it now
    if (SubmeshVertexLayout::eVertexStatic == layout) {
        Model::SubMesh<VertexStatic> submesh = {};

        submesh.indices = std::move(indices);
        submesh.layout = layout;

        constexpr size_t kBytesPerVertex = 15 * 4; // fields * bytes per field
        const auto num_vertices = vertex_buffer.size() / kBytesPerVertex;

        if (0ull != vertex_buffer.size() % kBytesPerVertex) {
            LogError("act: invalid vertex buffer size");
            return std::nullopt;
        }

        submesh.vertices.resize(num_vertices);
        BinaryReader vertex_reader{vertex_buffer};

        for (size_t i = 0; i < num_vertices; ++i) {
            READ_VEC3_PROPERTY_OR_ERROR(f32, vertex_reader, submesh.vertices[i].position);
            READ_VEC3_PROPERTY_OR_ERROR(f32, vertex_reader, submesh.vertices[i].normal);
            READ_VEC2_PROPERTY_OR_ERROR(f32, vertex_reader, submesh.vertices[i].texcoord);
            READ_VEC3_PROPERTY_OR_ERROR(f32, vertex_reader, submesh.vertices[i].color);
            READ_VEC4_PROPERTY_OR_ERROR(f32, vertex_reader, submesh.vertices[i].tangent);
        }

        return submesh;
    } else if (SubmeshVertexLayout::eVertexRigged == layout) {
        Model::SubMesh<VertexRigged> submesh = {};

        submesh.indices = std::move(indices);
        submesh.layout = layout;

        constexpr size_t kBytesPerVertex = 23 * 4; // fields * bytes per field
        const auto num_vertices = vertex_buffer.size() / kBytesPerVertex;

        if (0ull != vertex_buffer.size() % kBytesPerVertex) {
            LogError("act: invalid vertex buffer size");
            return std::nullopt;
        }

        submesh.vertices.resize(num_vertices);
        BinaryReader vertex_reader{vertex_buffer};

        for (size_t i = 0; i < num_vertices; ++i) {
            READ_VEC3_PROPERTY_OR_ERROR(f32, vertex_reader, submesh.vertices[i].position);
            READ_VEC3_PROPERTY_OR_ERROR(f32, vertex_reader, submesh.vertices[i].normal);
            READ_VEC2_PROPERTY_OR_ERROR(f32, vertex_reader, submesh.vertices[i].texcoord);
            READ_VEC3_PROPERTY_OR_ERROR(f32, vertex_reader, submesh.vertices[i].color);
            READ_VEC4_PROPERTY_OR_ERROR(f32, vertex_reader, submesh.vertices[i].tangent);
            READ_VEC4_PROPERTY_OR_ERROR(u32, vertex_reader, submesh.vertices[i].joints);
            READ_VEC4_PROPERTY_OR_ERROR(f32, vertex_reader, submesh.vertices[i].weights);
        }

        return submesh;
    } else {
        LogError("act: unsupported vertex layout type");
        return std::nullopt;
    }
}

auto parse_skin_block(std::span<const u8> block_buffer) -> std::optional<Model::Skin> {
    BinaryReader block_reader{block_buffer};
    Model::Skin skin = {};

    while (auto command_type = read_command_type(block_reader)) {
        switch (*command_type) {
        case act::CommandType::eSkinAddNode: {
            u32 node;
            SIZED_PROPERTY_ASSERT(block_reader, sizeof(u32));
            READ_OR_ERROR(u32, block_reader, node);
            skin.skin_node_ids.emplace_back(node);

            break;
        }

        default:
            LogError("act: invalid skin command type {}", static_cast<uint32_t>(*command_type));
            return std::nullopt;
        }
    }

    return skin;
}

auto parse_skin_node_block(std::span<const u8> block_buffer) -> std::optional<Model::SkinNode> {
    BinaryReader block_reader{block_buffer};
    Model::SkinNode skin_node = {};

    while (auto command_type = read_command_type(block_reader)) {
        switch (*command_type) {
        case act::CommandType::eSkinNodeSetNode:
            SIZED_PROPERTY_ASSERT(block_reader, sizeof(u32));
            READ_OR_ERROR(u32, block_reader, skin_node.node_id);
            break;

        case act::CommandType::eSkinNodeSetMatrix: {
            auto matrix_buffer = read_buffer(block_reader);
            if (!matrix_buffer.has_value() || 16 * sizeof(f32) != matrix_buffer->size_bytes()) {
                LogError("act: matrix buffer has invalid size, expected 16 x f32");
                return std::nullopt;
            }

            BinaryReader matrix_reader{matrix_buffer.value()};
            std::array<f32, 16> matrix_values;
            for (size_t i = 0; i < 16; ++i) {
                READ_OR_ERROR(f32, matrix_reader, matrix_values[i]);
            }

            skin_node.inverse_bind_matrix = glm::make_mat4x4(matrix_values.data());
            break;
        }

        default:
            LogError("act: invalid skin node command type {}", static_cast<uint32_t>(*command_type));
            return std::nullopt;
        }
    }

    return skin_node;
}

auto parse_animation_block(std::span<const u8> block_buffer) -> std::optional<Model::Animation> {
    BinaryReader block_reader{block_buffer};
    Model::Animation animation = {};

    while (auto command_type = read_command_type(block_reader)) {
        switch (*command_type) {
        case act::CommandType::eAnimationAddChannel: {
            u32 channel;
            SIZED_PROPERTY_ASSERT(block_reader, sizeof(u32));
            READ_OR_ERROR(u32, block_reader, channel);
            animation.channel_ids.emplace_back(channel);

            break;
        }

        default:
            LogError("act: invalid animation command type {}", static_cast<uint32_t>(*command_type));
            return std::nullopt;
        }
    }

    return animation;
}

template <AnimationPropertyType PropertyType>
auto parse_keyframe_data(std::span<const u8> buffer, const std::vector<f32> &timeline)
    -> std::optional<std::vector<typename Model::AnimationChannel<PropertyType>::Keyframe>> {
    using Channel = Model::AnimationChannel<PropertyType>;
    using DataType = Channel::Traits::DataType;

    const auto num_keyframes = timeline.size();
    BinaryReader reader{buffer};
    std::vector<typename Model::AnimationChannel<PropertyType>::Keyframe> keyframes{num_keyframes};

    if constexpr (std::is_same_v<glm::fvec3, DataType>) {
        for (size_t i = 0; i < num_keyframes; ++i) {
            keyframes[i].time = timeline[i];
            READ_VEC3_PROPERTY_OR_ERROR(f32, reader, keyframes[i].value);
        }
    } else if constexpr (std::is_same_v<glm::fquat, DataType>) {
        for (size_t i = 0; i < num_keyframes; ++i) {
            keyframes[i].time = timeline[i];
            READ_QUAT_PROPERTY_OR_ERROR(f32, reader, keyframes[i].value);
        }
    } else {
        static_assert(false, "unsupported keyframe type");
    }

    return keyframes;
}

auto parse_animation_channel_block(std::span<const u8> block_buffer) -> std::optional<Model::AnyAnimationChannel> {
    BinaryReader block_reader{block_buffer};

    std::optional<u32> node;
    AnimationPropertyType property;
    AnimationInterpolationMode mode;

    std::vector<f32> timeline;
    std::span<const u8> keyframe_buffer;

    while (auto command_type = read_command_type(block_reader)) {
        switch (*command_type) {
        case act::CommandType::eAnimChannelSetProp:
            SIZED_PROPERTY_ASSERT(block_reader, sizeof(u32));
            READ_ENUM_PROPERTY_OR_ERROR(parse_animation_property_type, block_reader, property);
            break;

        case act::CommandType::eAnimChannelSetMode:
            SIZED_PROPERTY_ASSERT(block_reader, sizeof(u32));
            READ_ENUM_PROPERTY_OR_ERROR(parse_animation_interpolation_mode, block_reader, mode);
            break;

        case act::CommandType::eAnimChannelSetNode:
            SIZED_PROPERTY_ASSERT(block_reader, sizeof(u32));
            READ_OR_ERROR(u32, block_reader, node);
            break;

        case act::CommandType::eAnimChannelSetKeyframes: {
            const auto buffer = read_buffer(block_reader);
            if (!buffer.has_value()) {
                LogError("act: missing keyframe buffer for animation node");
                return std::nullopt;
            }

            keyframe_buffer = buffer.value();
            break;
        }

        case act::CommandType::eAnimChannelSetTimeline: {
            u32 num_keyframes;
            READ_OR_ERROR(u32, block_reader, num_keyframes);

            num_keyframes = num_keyframes / sizeof(f32);
            timeline.reserve(num_keyframes);
            f32 time;

            for (auto i = 0u; i < num_keyframes; ++i) {
                READ_OR_ERROR(u32, block_reader, time);
                timeline.emplace_back(time);
            }

            break;
        }

        default:
            LogError("act: invalid animation channel command type {}", static_cast<uint32_t>(*command_type));
            return std::nullopt;
        }
    }

    if (!node.has_value()) {
        LogError("act: animation channel requires a node id");
        return std::nullopt;
    }

    switch (property) {
    case act::AnimationPropertyType::eTranslation: {
        auto keyframes = parse_keyframe_data<act::AnimationPropertyType::eTranslation>(keyframe_buffer, timeline);
        if (!keyframes.has_value()) {
            LogError("act: failed to parse keyframes");
            return std::nullopt;
        }

        Model::AnimationChannel<AnimationPropertyType::eTranslation> channel;
        channel.node_id = node.value();
        channel.interpolation = mode;
        channel.keyframes = std::move(keyframes.value());

        return channel;
    }

    case act::AnimationPropertyType::eRotation: {
        auto keyframes = parse_keyframe_data<act::AnimationPropertyType::eRotation>(keyframe_buffer, timeline);
        if (!keyframes.has_value()) {
            LogError("act: failed to parse keyframes");
            return std::nullopt;
        }

        Model::AnimationChannel<AnimationPropertyType::eRotation> channel;
        channel.node_id = node.value();
        channel.interpolation = mode;
        channel.keyframes = std::move(keyframes.value());

        return channel;
    }

    case act::AnimationPropertyType::eScale: {
        auto keyframes = parse_keyframe_data<act::AnimationPropertyType::eScale>(keyframe_buffer, timeline);
        if (!keyframes.has_value()) {
            LogError("act: failed to parse keyframes");
            return std::nullopt;
        }

        Model::AnimationChannel<AnimationPropertyType::eScale> channel;
        channel.node_id = node.value();
        channel.interpolation = mode;
        channel.keyframes = std::move(keyframes.value());

        return channel;
    }

    default:
        break;
    }

    return std::nullopt;
}

#define DEF_BLOCK_PARSER(T, parser, output)                                                                            \
    case T: {                                                                                                          \
        auto _block = parser(block_buffer.value());                                                                    \
        if (!_block.has_value()) {                                                                                     \
            LogError(                                                                                                  \
                "act: invalid block format for a block of type {} at {} of size {}", #T, block_position, block_size);  \
            return std::nullopt;                                                                                       \
        }                                                                                                              \
        output.emplace_back(std::move(_block.value()));                                                                \
        break;                                                                                                         \
    }

auto Model::load_from_binary(std::span<const u8> binary) -> std::optional<Model> {
    Model model;
    BinaryReader reader{binary};

    u32 magic, software_version, writer_version;
    READ_OR_ERROR(u32, reader, magic);
    READ_OR_ERROR(u32, reader, software_version);
    READ_OR_ERROR(u32, reader, writer_version);

    if (kMagicNumber != magic) {
        LogError("act: magic number mismatch");
        return std::nullopt;
    }

    if (kWriterSoftware != software_version || kWriterVersion != writer_version) {
        LogError("act: format version mismatch");
        return std::nullopt;
    }

    u32 model_size;
    READ_OR_ERROR(u32, reader, model_size);

    if (reader.remaining() < model_size) {
        LogError("act: expected remaining {} bytes, got {}", model_size, reader.remaining());
        return std::nullopt;
    }

    const auto start_position = reader.position();
    const auto end_position = start_position + model_size;
    while (reader.position() < end_position) {
        const auto block_position = reader.position();

        u32 block_type, block_size;
        READ_OR_ERROR(u32, reader, block_type);
        READ_OR_ERROR(u32, reader, block_size);

        if (reader.position() + block_size > end_position) {
            LogError("act: invalid block of size {} at {}", block_size, block_position);
            return std::nullopt;
        }

        const auto block_type_enum = parse_block_type(block_type);
        if (!block_type_enum.has_value()) {
            LogError("act: invalid block type {} of block at {}", block_type, block_position);
            return std::nullopt;
        }

        const auto block_buffer = reader.read_buffer(block_size);
        if (!block_buffer.has_value()) {
            LogError("act: invalid block, expected {} readable bytes in the stream", block_size);
            return std::nullopt;
        }

        switch (block_type_enum.value()) {
            // clang-format off
        DEF_BLOCK_PARSER(act::BlockType::eImageBlock,       parse_image_block,              model.images)
        DEF_BLOCK_PARSER(act::BlockType::eTextureBlock,     parse_texture_block,            model.textures)
        DEF_BLOCK_PARSER(act::BlockType::eMaterialBlock,    parse_material_block,           model.materials)
        DEF_BLOCK_PARSER(act::BlockType::eNodeBlock,        parse_node_block,               model.nodes)
        DEF_BLOCK_PARSER(act::BlockType::eMeshBlock,        parse_mesh_block,               model.meshes)
        DEF_BLOCK_PARSER(act::BlockType::eSubmeshBlock,     parse_submesh_block,            model.submeshes)
        DEF_BLOCK_PARSER(act::BlockType::eSkinBlock,        parse_skin_block,               model.skins)
        DEF_BLOCK_PARSER(act::BlockType::eSkinNodeBlock,    parse_skin_node_block,          model.skin_nodes)
        DEF_BLOCK_PARSER(act::BlockType::eAnimationBlock,   parse_animation_block,          model.animations)
        DEF_BLOCK_PARSER(act::BlockType::eAnimChannelBlock, parse_animation_channel_block,  model.animation_channels)
            // clang-format on

        default:
            LogWarning("act: skipping block of type {}, size {} bytes", block_type, block_size);
            break;
        }
    }

    LogInfo(
        "act: loaded a new model with statistics, nodes: {}, images: {}, textures: {}, submeshes: "
        "{}, animations: {}, anim channels: {}",
        model.nodes.size(), model.images.size(), model.textures.size(), model.submeshes.size(), model.animations.size(),
        model.animation_channels.size());

    return model;
}

} // namespace act
