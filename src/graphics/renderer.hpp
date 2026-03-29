#pragma once
#include <array>
#include <memory>
#include <optional>
#include <vector>

#include "vulkan.hpp"
#include "camera.hpp"

#include "common/utility.hpp"
#include "common/fixedqueue.hpp"

#undef near
#undef far

namespace graphics {

class Renderer final {
private:
    struct MeshTag {};
    struct TextureTag {};
    struct AnimatedMeshTag {};
    struct ActorMeshTag {};
    struct VectorMeshTag {};

    class BufferHelper {
    public:
        BufferHelper(Renderer *renderer, size_t size);
        virtual ~BufferHelper() noexcept;

        BufferHelper(const BufferHelper &) = delete;
        auto operator=(const BufferHelper &) = delete;

        BufferHelper(BufferHelper &&b) noexcept;
        auto operator=(BufferHelper &&b) noexcept -> BufferHelper &;

        auto size() const -> size_t { return size_; }
        auto stagingBuffer() const -> const Buffer & { return staging_buffer_; }
        auto deviceBuffer() const -> const Buffer & { return device_buffer_; }
        auto deviceAddress() const -> VkDeviceAddress { return device_address_; }
        auto cpuMappedPointer() const -> void * { return staging_buffer_.cpuMappedPointer(); }

        auto upload(VkCommandBuffer command_buffer, std::span<const uint8_t> data) -> void;

    private:
        Renderer *renderer_ = nullptr;
        Context *context_ = nullptr;

        size_t size_ = 0ull;

        Buffer staging_buffer_ = {};
        Buffer device_buffer_ = {};
        VkDeviceAddress device_address_ = {};
    };

    template <typename cbBufferDataType> class TypedBufferHelper : public BufferHelper {
    public:
        static constexpr auto kDataSize = sizeof(cbBufferDataType);
        static auto create(Renderer *renderer) -> std::unique_ptr<TypedBufferHelper> {
            return std::make_unique<TypedBufferHelper<cbBufferDataType>>(renderer);
        }

        TypedBufferHelper(Renderer *renderer) : BufferHelper{renderer, kDataSize} {}

        auto storage() -> cbBufferDataType & { return data_; }
        auto storage() const -> const cbBufferDataType & { return data_; }

        auto upload(VkCommandBuffer command_buffer) -> void {
            BufferHelper::upload(command_buffer, {reinterpret_cast<const uint8_t *>(&data_), kDataSize});
        }

    private:
        cbBufferDataType data_ = {};
    };

    template <typename T, typename Tag, uint32_t kPoolSize> class ResourcePool;
    class BindlessTexturePool;

    // simple helper class to help us with our descriptor pools
    // it is bound to the descriptor set layout, so it only holds
    // enough descriptors for N layouts, where N is the number of
    // frames in flight
    template <uint32_t kNumSets = 1ull> class DescriptorSetHelper;

public:
    static constexpr uint32_t kNumFramesInFlight = 2;
    static constexpr uint32_t kNumMaxPointLights = 20;
    static constexpr uint32_t kNumMaxStaticObjects = 1024;
    static constexpr uint32_t kNumMaxSkinnedObjects = 128;
    static constexpr uint32_t kNumMaxBonesPerObject = 200;
    static constexpr uint32_t kNumTexturePoolSize = 256;
    static constexpr uint32_t kNumMeshPoolSize = 512;
    static constexpr uint32_t kNumAnimMeshPoolSize = 128;
    static constexpr uint32_t kVertexBufferAlign = 64;
    static constexpr uint32_t kNumMaxVectorMeshes = 1024;
    static constexpr uint32_t kNumSamplesForMSAA = 4;

    struct cbFrameHeapBuffer {
        struct cbPointLightData {
            glm::fvec4 world_position;
            glm::fvec4 color_intensity;
        };

        struct cbStaticObjectData {
            glm::fmat4x4 world;
            int32_t diffuse_map;
            int32_t normal_map;
            int32_t reserved0;
            int32_t reserved1;
        };

        glm::fmat4x4 projection;
        glm::fmat4x4 view;

        glm::fmat4x4 projection_inv;
        glm::fmat4x4 view_inv;

        glm::fvec4 ambient;

        std::array<cbPointLightData, kNumMaxPointLights> point_lights;
        std::array<cbStaticObjectData, kNumMaxStaticObjects> static_objects;

        uint32_t num_lights, num_static_objects;
    };

    struct cbSkinningBuffer {
        glm::fmat4x4 bones[kNumMaxBonesPerObject];
    };

    struct cbVectorHeapBuffer {
        struct cbVectorObjectData {
            glm::fmat4x4 world;
            int32_t diffuse_map;
            int32_t reserved0;
            int32_t reserved1;
            int32_t reserved2;
        };

        glm::fmat4x4 view_projection;
        std::array<cbVectorObjectData, kNumMaxVectorMeshes> vector_objects;

        uint32_t num_vector_objects;
    };

    class Mesh;
    class Texture;
    class ActorMesh;

    template <typename cbBufferDataType> class DataBuffer final {
    public:
        DataBuffer(Renderer *renderer) : renderer_{renderer}, buffers_{makeBufferArray(renderer)} {}

        auto storage() -> cbBufferDataType & { return storage_; }
        auto storage() const -> const cbBufferDataType & { return storage_; }
        auto deviceAddress(uint32_t frame) -> VkDeviceAddress { return buffers_[frame].deviceAddress(); }

        auto upload(VkCommandBuffer command_buffer, uint32_t frame) -> void {
            buffers_[frame].upload(
                command_buffer, {reinterpret_cast<const uint8_t *>(storage_), sizeof(cbBufferDataType)});
        }

    private:
        static auto makeBufferArray(Renderer *renderer) -> std::array<BufferHelper, kNumFramesInFlight> {
            return [&]<size_t... I>(std::index_sequence<I...>) {
                return std::array<BufferHelper, kNumFramesInFlight>{
                    (void(I), BufferHelper{renderer, sizeof(cbBufferDataType)})...};
            }(std::make_index_sequence<kNumFramesInFlight>{});
        }

        Renderer *renderer_ = nullptr;

        cbBufferDataType storage_;
        std::array<BufferHelper, kNumFramesInFlight> buffers_;
    };

    template <typename cbBufferDataType> class SharedDataBuffer final {
    public:
        static auto create(Renderer *renderer) -> std::optional<SharedDataBuffer> {
            SharedDataBuffer<cbBufferDataType> shared_buffer_helper;

            shared_buffer_helper.renderer_ = renderer;
            for (auto &buffer : shared_buffer_helper.buffers_) {
                buffer.buffer = shared_buffer_helper.renderer_->context_->memory().createSharedBuffer(
                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, sizeof(cbBufferDataType));

                buffer.address = buffer.buffer.deviceAddress();
            }

            return shared_buffer_helper;
        }

        auto storage() -> cbBufferDataType & { return storage_; }
        auto storage() const -> const cbBufferDataType & { return storage_; }
        auto deviceAddress(uint32_t frame) const -> VkDeviceAddress { return buffers_[frame].address; }

        auto upload(uint32_t frame) const -> void {
            ::memcpy(buffers_[frame].buffer.cpuMappedPointer(), &storage_, sizeof(cbBufferDataType));
        }

    private:
        struct SharedBufferWithAddress {
            Buffer buffer;
            VkDeviceAddress address;
        };

        SharedDataBuffer() = default;

        Renderer *renderer_ = nullptr;

        cbBufferDataType storage_;
        std::array<SharedBufferWithAddress, kNumFramesInFlight> buffers_;
    };

    class IShaderLoader {
    public:
        IShaderLoader() = default;

        virtual ~IShaderLoader() = default;
        virtual auto loadSkinningPassShader() const -> std::optional<std::vector<uint32_t>> = 0;
        virtual auto loadGeometryPassShader() const -> std::optional<std::vector<uint32_t>> = 0;
        virtual auto loadVectorPassShader() const -> std::optional<std::vector<uint32_t>> = 0;
        virtual auto loadLightingPassShader() const -> std::optional<std::vector<uint32_t>> = 0;
        virtual auto loadInterfacePassShader() const -> std::optional<std::vector<uint32_t>> = 0;

        IShaderLoader(const IShaderLoader &) = delete;
        auto operator=(const IShaderLoader &) = delete;

        IShaderLoader(IShaderLoader &&) noexcept = delete;
        auto operator=(IShaderLoader &&) noexcept = delete;
    };

    template <typename T, typename Tag> class ResourceId final {
    public:
        using Resource = T;

        ResourceId(uint32_t index, uint32_t generation) : index_{index}, generation_{generation} {}

        auto index() const -> uint32_t { return index_; }
        auto generation() const -> uint32_t { return generation_; }

    private:
        uint32_t index_;
        uint32_t generation_;
    };

    using MeshId = ResourceId<Mesh, MeshTag>;
    using TextureId = ResourceId<Texture, TextureTag>;
    using AnimatedMeshId = ResourceId<Mesh, AnimatedMeshTag>;
    using ActorMeshId = ResourceId<ActorMesh, ActorMeshTag>;
    using VectorMeshId = ResourceId<Mesh, VectorMeshTag>;

    struct Description {
        Context *context;
        std::unique_ptr<IShaderLoader> shader_loader;
    };

    struct OpaqueDrawDescription {
        MeshId mesh;
        glm::fmat4x4 world_matrix;

        std::optional<TextureId> diffuse_map;
        std::optional<TextureId> normal_map;
    };

    struct SkinnedDrawDescription {
        ActorMeshId skinned_mesh;

        glm::fmat4x4 world_matrix;

        std::optional<TextureId> diffuse_map;
        std::optional<TextureId> normal_map;
    };

    struct VectorDrawDescription {
        VectorMeshId vector_mesh;
        glm::fmat4x4 world_matrix;

        std::optional<TextureId> diffuse_map;
    };

    struct VectorVertex {
        glm::fvec3 position;
        glm::fvec2 uv;
        glm::fvec4 color;

        VectorVertex(const glm::fvec2 &position, const glm::fvec2 &uv)
            : position{position, 0.0f}, uv{uv}, color{1.0f} {}

        VectorVertex(const glm::fvec2 &position, const glm::fvec4 &color)
            : position{position, 0.0f}, uv{0.0f}, color{color} {}

        VectorVertex(const glm::fvec3 &position, const glm::fvec2 &uv) : position{position}, uv{uv}, color{1.0f} {}

        VectorVertex(const glm::fvec3 &position, const glm::fvec4 &color)
            : position{position}, uv{0.0f}, color{color} {}

        VectorVertex(const glm::fvec3 &position, const glm::fvec2 &uv, const glm::fvec4 &color)
            : position{position}, uv{uv}, color{color} {}

        VectorVertex(float x, float y, float z, float u, float v) : position{x, y, z}, uv{u, v}, color{1.0f} {}
        VectorVertex(float x, float y, float z, float r, float g, float b, float a)
            : position{x, y, z}, uv{0.0f}, color{r, g, b, a} {}

        VectorVertex(float x, float y, float z, float u, float v, float r, float g, float b, float a)
            : position{x, y, z}, uv{u, v}, color{r, g, b, a} {}
    };

    struct StaticVertex {
        glm::fvec4 position; // vec3 + pad
        glm::fvec4 normal;   // vec3 + pad
        glm::fvec4 uv;       // vec2 + pad
        glm::fvec4 color;    // vec3 + pad
        glm::fvec4 tangent;

        StaticVertex() = default;

        StaticVertex(
            const glm::fvec3 &pos, const glm::fvec3 &norm, const glm::fvec2 &uv_coords, const glm::fvec3 &col,
            const glm::fvec4 &tan)
            : position{pos, 0.0f}, normal{norm, 0.0f}, uv{uv_coords, 0.0f, 0.0f}, color{col, 1.0f}, tangent{tan} {}

        StaticVertex(const glm::fvec3 &pos, const glm::fvec3 &norm, const glm::fvec2 &uv_coords, const glm::fvec4 &tan)
            : position{pos, 0.0f}, normal{norm, 0.0f}, uv{uv_coords, 0.0f, 0.0f}, color{1.0f, 1.0f, 1.0f, 1.0f},
              tangent{tan} {}

        StaticVertex(
            float x, float y, float z, float nx, float ny, float nz, float u, float v, float tx, float ty, float tz,
            float tw)
            : position{x, y, z, 0.0f}, normal{nx, ny, nz, 0.0f}, uv{u, v, 0.0f, 0.0f}, color{1.0f, 1.0f, 1.0f, 1.0f},
              tangent{tx, ty, tz, tw} {}

        StaticVertex(
            float x, float y, float z, float nx, float ny, float nz, float u, float v, float r, float g, float b,
            float tx, float ty, float tz, float tw)
            : position{x, y, z, 0.0f}, normal{nx, ny, nz, 0.0f}, uv{u, v, 0.0f, 0.0f}, color{r, g, b, 1.0f},
              tangent{tx, ty, tz, tw} {}
    };

    struct SkinnedVertex {
        glm::fvec4 position; // vec3 + pad
        glm::fvec4 normal;   // vec3 + pad
        glm::fvec4 uv;       // vec2 + pad
        glm::fvec4 color;    // vec3 + pad
        glm::fvec4 tangent;
        glm::uvec4 bones;
        glm::fvec4 weights;

        SkinnedVertex() = default;

        SkinnedVertex(
            const glm::fvec3 &pos, const glm::fvec3 &norm, const glm::fvec2 &uv_coords, const glm::fvec3 &col,
            const glm::fvec4 &tan, const glm::uvec4 &bones, const glm::fvec4 &weights)
            : position{pos.x, pos.y, pos.z, 0.0f}, normal{norm.y, norm.y, norm.z, 0.0f}, uv{uv_coords, 0.0f, 0.0f},
              color{col.x, col.y, col.z, 0.0f}, tangent{tan}, bones{bones}, weights{weights} {}
    };

    enum PerFrameDescriptors : uint32_t {
        PerFrameTexturePool = 0,
    };

    class Mesh final {
    public:
        struct Description {
            std::span<const uint8_t> vertex_buffer;
            std::span<const uint32_t> indices;

            uint32_t vertex_size;
            uint32_t num_vertices;

            VkBufferUsageFlags vertex_buffer_flags;
            VkBufferUsageFlags index_buffer_flags;
        };

        Mesh(const Mesh &) = delete;
        auto operator=(const Mesh &) = delete;

        Mesh(Mesh &&) noexcept;
        auto operator=(Mesh &&) noexcept -> Mesh &;

        ~Mesh() noexcept = default;

        auto numVertices() const -> uint32_t { return num_vertices_; }
        auto numIndices() const -> uint32_t { return num_indices_; }

        auto vertexBuffer() const -> const Buffer & { return vertex_buffer_; }
        auto indexBuffer() const -> const Buffer & { return index_bufer_; }

        auto vertexBufferSize() const -> VkDeviceSize { return vertex_buffer_size_; }
        auto indexBufferSize() const -> VkDeviceSize { return index_buffer_size_; }

    private:
        static auto create(Renderer *renderer, const Description &desc) -> std::optional<Mesh>;

        Mesh() = default;

        Renderer *renderer_ = nullptr;
        uint32_t num_vertices_, num_indices_;

        Buffer vertex_buffer_;
        Buffer index_bufer_;

        VkDeviceSize vertex_buffer_size_;
        VkDeviceSize index_buffer_size_;

        friend class Renderer;
    };

    class Texture final {
    public:
        enum class MagFilter {
            eNearest,
            eLinear,
        };

        enum class MinFilter {
            eNearest,
            eLinear,
        };

        struct Description {
            uint32_t width;
            uint32_t height;
            MagFilter mag_filter;
            MinFilter min_filter;
            bool is_target;
            uint32_t samples;
            VkFormat format;
        };

        Texture(const Texture &) = delete;
        auto operator=(const Texture &) = delete;

        Texture(Texture &&) noexcept;
        auto operator=(Texture &&) noexcept -> Texture &;

        ~Texture() noexcept;

        auto description() const -> const Description & { return description_; }
        auto image() const -> const Image & { return image_; }
        auto imageView() const -> const Image::View & { return image_view_; }
        auto sampler() const -> VkSampler { return sampler_; }

    private:
        static auto create(Renderer *renderer, const Description &desc) -> std::optional<Texture>;
        static auto fromRgba(Renderer *renderer, const Description &desc, std::span<const uint8_t> rgba_data)
            -> std::optional<Texture>;

        Texture() = default;

        Renderer *renderer_ = nullptr;
        Description description_;

        Image image_;
        Image::View image_view_;
        VkSampler sampler_ = VK_NULL_HANDLE;

        friend class Renderer;
    };

    class ActorMesh final {
    public:
        ~ActorMesh() noexcept = default;

        ActorMesh(const ActorMesh &) = delete;
        auto operator=(const ActorMesh &) = delete;

        ActorMesh(ActorMesh &&) noexcept = default;
        auto operator=(ActorMesh &&) noexcept -> ActorMesh & = default;

        auto inputMesh() const -> AnimatedMeshId { return input_mesh_; }
        auto vertexBuffer(uint32_t current_frame) const -> const Buffer & { return output_buffer_[current_frame]; }
        auto transformBuffer() const -> const SharedDataBuffer<cbSkinningBuffer> & { return buffer_; }

        auto skinningBuffer() const -> const cbSkinningBuffer & { return buffer_.storage(); }
        auto skinningBuffer() -> cbSkinningBuffer & { return buffer_.storage(); }

    private:
        static auto create(Renderer *renderer, AnimatedMeshId mesh) -> std::optional<ActorMesh>;

        ActorMesh(Renderer *renderer, AnimatedMeshId input_mesh, SharedDataBuffer<cbSkinningBuffer> &&buffer)
            : renderer_{renderer}, input_mesh_{input_mesh}, buffer_{std::move(buffer)} {};

        Renderer *renderer_ = nullptr;

        AnimatedMeshId input_mesh_;
        std::array<Buffer, kNumFramesInFlight> output_buffer_;

        VkDeviceSize output_buffer_size_ = 0;
        size_t num_vertices_ = 0;

        SharedDataBuffer<cbSkinningBuffer> buffer_;

        friend class Renderer;
    };

    static auto create(const Description &description) -> std::unique_ptr<Renderer>;

    ~Renderer() noexcept;

    Renderer(const Renderer &) = delete;
    auto operator=(const Renderer &) = delete;

    Renderer(Renderer &&) noexcept = delete;
    auto operator=(Renderer &&) noexcept = delete;

    auto camera() -> Camera & { return camera_; }
    auto camera() const -> const Camera & { return camera_; }

    auto swapchainNeedsUpdate() const -> bool { return swapchain_needs_update_; }

    auto resize(VkExtent2D surface_extent, VkExtent2D framebuffer_extent) {
        if (surface_extent.width == 0 || surface_extent.height == 0 || framebuffer_extent.width == 0 ||
            framebuffer_extent.height == 0) {
            return;
        }

        pending_resize_ = {surface_extent, framebuffer_extent};
    }

    template <util::TypedContiguousRange<StaticVertex> VR, util::TypedContiguousRange<const uint32_t> IR>
    auto createMesh(const VR &vertex_input_range, const IR &index_input_range) -> std::optional<MeshId> {
        const auto vertex_buffer_ptr = reinterpret_cast<const uint8_t *>(std::ranges::data(vertex_input_range));
        const auto vertex_buffer_size = std::ranges::size(vertex_input_range) * sizeof(StaticVertex);

        const auto index_buffer_ptr = std::ranges::data(index_input_range);
        const auto index_buffer_size = std::ranges::size(index_input_range);

        Mesh::Description desc = {
            .vertex_buffer = {vertex_buffer_ptr, vertex_buffer_size},
            .indices = {index_buffer_ptr, index_buffer_size},
            .vertex_size = sizeof(StaticVertex),
            .num_vertices = static_cast<uint32_t>(std::ranges::size(vertex_input_range)),
            .vertex_buffer_flags = 0,
            .index_buffer_flags = 0,
        };

        auto mesh = Mesh::create(this, desc);

        if (!mesh.has_value()) {
            return std::nullopt;
        }

        return addMesh(std::move(mesh.value()));
    }

    template <util::TypedContiguousRange<const uint8_t> R>
    auto createRgbaTexture(const Texture::Description &desc, const R &rgba_data) -> std::optional<TextureId> {
        const auto rgba_buffer_ptr = std::ranges::data(rgba_data);
        const auto rgba_buffer_size = std::ranges::size(rgba_data);

        auto texture = Texture::fromRgba(this, desc, {rgba_buffer_ptr, rgba_buffer_size});
        if (!texture.has_value()) {
            return std::nullopt;
        }

        return addTexture(std::move(texture.value()));
    }

    template <util::TypedContiguousRange<SkinnedVertex> VR, util::TypedContiguousRange<const uint32_t> IR>
    auto createAnimatedMesh(const VR &vertex_input_range, const IR &index_input_range)
        -> std::optional<AnimatedMeshId> {
        const auto vertex_buffer_ptr = reinterpret_cast<const uint8_t *>(std::ranges::data(vertex_input_range));
        const auto vertex_buffer_size = std::ranges::size(vertex_input_range) * sizeof(SkinnedVertex);

        const auto index_buffer_ptr = std::ranges::data(index_input_range);
        const auto index_buffer_size = std::ranges::size(index_input_range);

        Mesh::Description desc = {
            .vertex_buffer = {vertex_buffer_ptr, vertex_buffer_size},
            .indices = {index_buffer_ptr, index_buffer_size},
            .vertex_size = sizeof(SkinnedVertex),
            .num_vertices = static_cast<uint32_t>(std::ranges::size(vertex_input_range)),
            .vertex_buffer_flags = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            .index_buffer_flags = 0,
        };

        auto mesh = Mesh::create(this, desc);

        if (!mesh.has_value()) {
            return std::nullopt;
        }

        return addAnimMesh(std::move(mesh.value()));
    }

    auto createActorMesh(AnimatedMeshId mesh) -> std::optional<ActorMeshId> {
        auto actor_mesh = ActorMesh::create(this, mesh);

        if (!actor_mesh.has_value()) {
            return std::nullopt;
        }

        return addActorMesh(std::move(actor_mesh.value()));
    }

    template <util::TypedContiguousRange<VectorVertex> VR, util::TypedContiguousRange<const uint32_t> IR>
    auto createVectorMesh(const VR &vertex_input_range, const IR &index_input_range) -> std::optional<VectorMeshId> {
        const auto vertex_buffer_ptr = reinterpret_cast<const uint8_t *>(std::ranges::data(vertex_input_range));
        const auto vertex_buffer_size = std::ranges::size(vertex_input_range) * sizeof(VectorVertex);

        const auto index_buffer_ptr = std::ranges::data(index_input_range);
        const auto index_buffer_size = std::ranges::size(index_input_range);

        Mesh::Description desc = {
            .vertex_buffer = {vertex_buffer_ptr, vertex_buffer_size},
            .indices = {index_buffer_ptr, index_buffer_size},
            .vertex_size = sizeof(VectorVertex),
            .num_vertices = static_cast<uint32_t>(std::ranges::size(vertex_input_range)),
            .vertex_buffer_flags = 0,
            .index_buffer_flags = 0,
        };

        auto mesh = Mesh::create(this, desc);

        if (!mesh.has_value()) {
            return std::nullopt;
        }

        return addVectorMesh(std::move(mesh.value()));
    }

    template <std::invocable<const Texture &> F> auto withTexture(TextureId handle, F consumer) const {
        auto texture = getTexture(handle);
        if (!texture) {
            return;
        }

        consumer(*texture);
    }

    template <std::invocable<const Mesh &> F> auto withMesh(MeshId handle, F consumer) const {
        auto mesh = getMesh(handle);
        if (!mesh) {
            return;
        }

        consumer(*mesh);
    }

    template <std::invocable<const Mesh &> F> auto withAnimMesh(AnimatedMeshId handle, F consumer) const {
        auto anim_mesh = getAnimMesh(handle);
        if (!anim_mesh) {
            return;
        }

        consumer(*anim_mesh);
    }

    template <std::invocable<const ActorMesh &> F> auto withActorMesh(ActorMeshId handle, F consumer) const {
        auto actor_mesh = getActorMesh(handle);
        if (!actor_mesh) {
            return;
        }

        consumer(*actor_mesh);
    }

    template <std::invocable<ActorMesh &> F> auto withActorMeshMut(ActorMeshId handle, F consumer) {
        auto actor_mesh = getActorMesh(handle);
        if (!actor_mesh) {
            return;
        }

        consumer(*actor_mesh);
    }

    template <std::invocable<const Mesh &> F> auto withVectorMesh(VectorMeshId handle, F consumer) {
        auto vector_mesh = getVectorMesh(handle);
        if (!vector_mesh) {
            return;
        }

        consumer(*vector_mesh);
    }

    auto deleteMesh(MeshId handle) { unrefMesh(handle); }
    auto deleteTexture(TextureId handle) { unrefTexture(handle); }
    auto deleteAnimMesh(AnimatedMeshId handle) { unrefAnimMesh(handle); }
    auto deleteActorMesh(ActorMeshId handle) { unrefActorMesh(handle); }
    auto deleteVectorMesh(VectorMeshId handle) { unrefVectorMesh(handle); }

    auto getMesh(const MeshId &id) const -> const Mesh *;
    auto getTexture(const TextureId &id) const -> const Texture *;
    auto getAnimMesh(const AnimatedMeshId &id) const -> const Mesh *;
    auto getActorMesh(const ActorMeshId &id) const -> const ActorMesh *;
    auto getActorMesh(const ActorMeshId &id) -> ActorMesh *;
    auto getVectorMesh(const VectorMeshId &id) const -> const Mesh *;

    auto frame() -> util::Result;

    auto drawOpaqueMesh(OpaqueDrawDescription &&desc) -> util::Result { return draw_queue_.push(std::move(desc)); }

    auto drawSkinnedMesh(SkinnedDrawDescription &&desc) -> util::Result {
        return skinning_queue_.push(std::move(desc));
    }

    auto drawVectorMesh(VectorDrawDescription &&desc) -> util::Result { return vector_queue_.push(std::move(desc)); }

private:
    class ComputeSkinningPass final {
    public:
        struct cbPushConstantBuffer {
            VkDeviceAddress bone_buffer;
            VkDeviceAddress input_buffer;
            VkDeviceAddress output_buffer;
        };

        static auto create(Renderer *renderer, const IShaderLoader *shader_loader)
            -> std::unique_ptr<ComputeSkinningPass>;

        ~ComputeSkinningPass() noexcept;

        ComputeSkinningPass(const ComputeSkinningPass &) = delete;
        auto operator=(const ComputeSkinningPass &) = delete;

        ComputeSkinningPass(ComputeSkinningPass &&) noexcept = delete;
        auto operator=(ComputeSkinningPass &&) noexcept = delete;

        auto shaderModule() const -> VkShaderModule { return shader_module_; }
        auto pipelineLayout() const -> VkPipelineLayout { return pipeline_layout_; }
        auto pipeline() const -> VkPipeline { return pipeline_; }

    private:
        ComputeSkinningPass() = default;

        Renderer *renderer_ = nullptr;

        VkShaderModule shader_module_ = VK_NULL_HANDLE;
        VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
        VkPipeline pipeline_ = VK_NULL_HANDLE;
    };

    class OpaqueGeometryPass final {
    public:
        struct cbPushConstantBuffer {
            VkDeviceAddress frame_heap;
            uint32_t object_id;
        };

        static auto create(Renderer *renderer, const IShaderLoader *shader_loader)
            -> std::unique_ptr<OpaqueGeometryPass>;

        ~OpaqueGeometryPass() noexcept;

        OpaqueGeometryPass(const OpaqueGeometryPass &) = delete;
        auto operator=(const OpaqueGeometryPass &) = delete;

        OpaqueGeometryPass(OpaqueGeometryPass &&) noexcept = delete;
        auto operator=(OpaqueGeometryPass &&) noexcept = delete;

        auto shaderModule() const -> VkShaderModule { return shader_module_; }
        auto pipelineLayout() const -> VkPipelineLayout { return pipeline_layout_; }
        auto pipeline() const -> VkPipeline { return pipeline_; }

    private:
        OpaqueGeometryPass() = default;

        Renderer *renderer_ = nullptr;

        VkShaderModule shader_module_ = VK_NULL_HANDLE;
        VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
        VkPipeline pipeline_ = VK_NULL_HANDLE;
    };

    class VectorGraphicsPass final {
    public:
        struct cbPushConstantBuffer {
            VkDeviceAddress frame_heap;
            uint32_t object_id;
        };

        static auto create(Renderer *renderer, const IShaderLoader *shader_loader)
            -> std::unique_ptr<VectorGraphicsPass>;

        ~VectorGraphicsPass() noexcept;

        VectorGraphicsPass(const VectorGraphicsPass &) = delete;
        auto operator=(const VectorGraphicsPass &) = delete;

        VectorGraphicsPass(VectorGraphicsPass &&) noexcept = delete;
        auto operator=(VectorGraphicsPass &&) noexcept = delete;

        auto shaderModule() const -> VkShaderModule { return shader_module_; }
        auto pipelineLayout() const -> VkPipelineLayout { return pipeline_layout_; }
        auto pipeline() const -> VkPipeline { return pipeline_; }

    private:
        VectorGraphicsPass() = default;

        Renderer *renderer_ = nullptr;

        VkShaderModule shader_module_ = VK_NULL_HANDLE;
        VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
        VkPipeline pipeline_ = VK_NULL_HANDLE;
    };

    class FullscreenPass final {
    public:
        static auto create(Renderer *renderer, uint32_t push_constant_size, std::span<const uint32_t> shader_bytecode)
            -> std::unique_ptr<FullscreenPass>;

        ~FullscreenPass() noexcept;

        FullscreenPass(const FullscreenPass &) = delete;
        auto operator=(const FullscreenPass &) = delete;

        FullscreenPass(FullscreenPass &&) noexcept = delete;
        auto operator=(FullscreenPass &&) noexcept = delete;

        auto shaderModule() const -> VkShaderModule { return shader_module_; }
        auto pipelineLayout() const -> VkPipelineLayout { return pipeline_layout_; }
        auto pipeline() const -> VkPipeline { return pipeline_; }

    private:
        FullscreenPass() = default;

        Renderer *renderer_ = nullptr;

        VkShaderModule shader_module_ = VK_NULL_HANDLE;
        VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
        VkPipeline pipeline_ = VK_NULL_HANDLE;
    };

    struct cbUserInterfacePassConstants {
        uint32_t texture;
        uint32_t reserved0;
        uint32_t reserved1;
        uint32_t reserved2;
    };

    struct cbLightingPassConstants {
        uint32_t texture;
        uint32_t reserved0;
        uint32_t reserved1;
        uint32_t reserved2;
    };

    struct FrameData {
        std::unique_ptr<TypedBufferHelper<cbFrameHeapBuffer>> scene_buffer;
        std::unique_ptr<TypedBufferHelper<cbVectorHeapBuffer>> vector_buffer;
        VkCommandBuffer command_buffer = VK_NULL_HANDLE;

        VkFence fence = VK_NULL_HANDLE;
        VkSemaphore present_semaphore = VK_NULL_HANDLE;

        std::optional<TextureId> geometry_target;
        std::optional<TextureId> vector_target_msaa;
        std::optional<TextureId> vector_target;
    };

    auto addMesh(Mesh &&mesh) -> std::optional<MeshId>;
    auto addTexture(Texture &&texture) -> std::optional<TextureId>;
    auto addAnimMesh(Mesh &&mesh) -> std::optional<AnimatedMeshId>;
    auto addActorMesh(ActorMesh &&mesh) -> std::optional<ActorMeshId>;
    auto addVectorMesh(Mesh &&mesh) -> std::optional<VectorMeshId>;

    auto unrefMesh(const MeshId &id) -> void;
    auto unrefTexture(const TextureId &id) -> void;
    auto unrefAnimMesh(const AnimatedMeshId &id) -> void;
    auto unrefActorMesh(const ActorMeshId &id) -> void;
    auto unrefVectorMesh(const VectorMeshId &id) -> void;

    auto createSwapchainData() -> void;
    auto createRenderTargets() -> void;
    auto getCurrentFrame() -> FrameData & { return frames_[current_frame_]; }

    Renderer() = default;

    Context *context_ = nullptr;
    Camera camera_;

    Image depth_buffer_;
    Image::View depth_buffer_view_;

    struct SwapchainImageData {
        VkSemaphore render_semaphore = VK_NULL_HANDLE;
    };

    struct PendingResize {
        VkExtent2D surface_extent, framebuffer_extent;
    };

    VkCommandPool command_pool_ = VK_NULL_HANDLE;

    std::optional<PendingResize> pending_resize_;
    bool swapchain_needs_update_ = false;

    std::array<FrameData, kNumFramesInFlight> frames_;
    std::vector<SwapchainImageData> swapchain_data_;

    // resource pools
    std::unique_ptr<BindlessTexturePool> texture_pool_;
    std::unique_ptr<ResourcePool<Mesh, MeshTag, kNumMeshPoolSize>> mesh_pool_;
    std::unique_ptr<ResourcePool<Mesh, AnimatedMeshTag, kNumAnimMeshPoolSize>> anim_mesh_pool_;
    std::unique_ptr<ResourcePool<ActorMesh, ActorMeshTag, kNumMaxSkinnedObjects>> actor_mesh_pool_;
    std::unique_ptr<ResourcePool<Mesh, VectorMeshTag, kNumMaxVectorMeshes>> vector_mesh_pool_;

    // render passes
    std::unique_ptr<ComputeSkinningPass> skinning_pass_ = nullptr;
    std::unique_ptr<OpaqueGeometryPass> geometry_pass_ = nullptr;
    std::unique_ptr<VectorGraphicsPass> vector_pass_ = nullptr;
    std::unique_ptr<FullscreenPass> lighting_pass_ = nullptr;
    std::unique_ptr<FullscreenPass> interface_pass_ = nullptr;

    // draw queue
    util::FixedSizeQueue<SkinnedDrawDescription, kNumMaxSkinnedObjects> skinning_queue_;
    util::FixedSizeQueue<OpaqueDrawDescription, kNumMaxStaticObjects> draw_queue_;
    util::FixedSizeQueue<VectorDrawDescription, kNumMaxVectorMeshes> vector_queue_;

    uint32_t current_frame_ = 0;
};

} // namespace graphics