#pragma once
#include <array>
#include <memory>
#include <optional>
#include <vector>

#include "vulkan.hpp"
#include "camera.hpp"

#include "common/utility.hpp"
#include "common/fixedqueue.hpp"
#include "common/managedpool.hpp"

#include "graphics/renderer/bindless.hpp"
#include "graphics/renderer/scheduler.hpp"
#include "graphics/renderer/texture.hpp"
#include "graphics/renderer/buffer.hpp"
#include "graphics/renderer/pipeline.hpp"

namespace graphics {

class Renderer final {
private:
    struct MeshTag {};
    struct TextureTag {};
    struct AnimatedMeshTag {};
    struct ActorMeshTag {};
    struct VectorMeshTag {};

public:
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

    using TextureId = BindlessTexturePool<kNumTexturePoolSize>::Id;

    class Mesh final {
    public:
        struct Description {
            std::span<const uint8_t> vertex_buffer;
            std::span<const uint32_t> indices;

            uint32_t vertex_size;
            uint32_t num_vertices;

            RendererBuffer::Usage vertex_buffer_flags;
            RendererBuffer::Usage index_buffer_flags;
        };

        Mesh(const Mesh &) = delete;
        auto operator=(const Mesh &) = delete;

        Mesh(Mesh &&) noexcept;
        auto operator=(Mesh &&) noexcept -> Mesh &;

        ~Mesh() noexcept = default;

        auto numVertices() const -> uint32_t { return num_vertices_; }
        auto numIndices() const -> uint32_t { return num_indices_; }

        auto vertexBuffer() -> RendererBuffer * { return vertex_buffer_; }
        auto indexBuffer() -> RendererBuffer * { return index_bufer_; }

        auto vertexBuffer() const -> const RendererBuffer * { return vertex_buffer_; }
        auto indexBuffer() const -> const RendererBuffer * { return index_bufer_; }

        auto vertexBufferSize() const -> VkDeviceSize { return vertex_buffer_->description().size; }
        auto indexBufferSize() const -> VkDeviceSize { return index_bufer_->description().size; }

    private:
        static auto create(Renderer *renderer, const Description &desc) -> std::optional<Mesh>;

        Mesh() = default;

        Renderer *renderer_ = nullptr;
        uint32_t num_vertices_, num_indices_;

        util::RefCountedPtr<RendererBuffer> vertex_buffer_;
        util::RefCountedPtr<RendererBuffer> index_bufer_;

        friend class Renderer;
    };

    using MeshId = util::ManagedPool<Mesh, kNumMeshPoolSize, MeshTag>::Id;
    using AnimatedMeshId = util::ManagedPool<Mesh, kNumAnimMeshPoolSize, AnimatedMeshTag>::Id;
    using VectorMeshId = util::ManagedPool<Mesh, kNumMaxVectorMeshes, VectorMeshTag>::Id;

    class ActorMesh final {
    public:
        ~ActorMesh() noexcept = default;

        ActorMesh(const ActorMesh &) = delete;
        auto operator=(const ActorMesh &) = delete;

        ActorMesh(ActorMesh &&) noexcept = default;
        auto operator=(ActorMesh &&) noexcept -> ActorMesh & = default;

        auto inputMesh() const -> AnimatedMeshId { return input_mesh_; }
        auto vertexBuffer() -> RendererBuffer * { return output_buffer_->getCurrent(); }
        auto vertexBuffer() const -> const RendererBuffer * { return output_buffer_->getCurrent(); }
        auto transformBuffer() -> MutableSharedBuffer<cbSkinningBuffer> & { return buffer_; }
        auto transformBuffer() const -> const MutableSharedBuffer<cbSkinningBuffer> & { return buffer_; }
        auto skinningBuffer() const -> const cbSkinningBuffer & { return buffer_.data(); }
        auto skinningBuffer() -> cbSkinningBuffer & { return buffer_.data(); }

    private:
        static auto create(Renderer *renderer, AnimatedMeshId mesh) -> std::optional<ActorMesh>;

        ActorMesh(AnimatedMeshId input_mesh, MutableSharedBuffer<cbSkinningBuffer> &&buffer)
            : input_mesh_{input_mesh}, buffer_{std::move(buffer)} {};

        AnimatedMeshId input_mesh_;
        std::unique_ptr<RendererScheduler::MutableBuffer> output_buffer_;

        VkDeviceSize output_buffer_size_ = 0;
        size_t num_vertices_ = 0;

        MutableSharedBuffer<cbSkinningBuffer> buffer_;

        friend class Renderer;
    };

    using ActorMeshId = util::ManagedPool<ActorMesh, kNumMaxSkinnedObjects, ActorMeshTag>::Id;

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

    struct Description {
        Context *context;
        RendererScheduler *scheduler;
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

    static auto create(const Description &description) -> std::unique_ptr<Renderer>;

    ~Renderer() noexcept;

    Renderer(const Renderer &) = delete;
    auto operator=(const Renderer &) = delete;

    Renderer(Renderer &&) noexcept = delete;
    auto operator=(Renderer &&) noexcept = delete;

    auto scheduler() -> RendererScheduler & { return *scheduler_; }
    auto scheduler() const -> const RendererScheduler & { return *scheduler_; }

    auto camera() -> Camera & { return camera_; }
    auto camera() const -> const Camera & { return camera_; }

    auto resize(VkExtent2D surface_extent, VkExtent2D framebuffer_extent) {
        if (surface_extent.width == 0 || surface_extent.height == 0 || framebuffer_extent.width == 0 ||
            framebuffer_extent.height == 0) {
            return;
        }

        pending_resize_ = {surface_extent, framebuffer_extent};
    }

    auto createRgbaTexture(const RendererTexture::RgbaDescription &desc) -> std::optional<TextureId>;

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
            .vertex_buffer_flags = RendererBuffer::Usage::eNone,
            .index_buffer_flags = RendererBuffer::Usage::eNone,
        };

        auto mesh = Mesh::create(this, desc);

        if (!mesh.has_value()) {
            return std::nullopt;
        }

        return mesh_pool_.store(std::move(mesh.value()));
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
            .vertex_buffer_flags = RendererBuffer::Usage::eBufferDeviceAddress,
            .index_buffer_flags = RendererBuffer::Usage::eNone,
        };

        auto mesh = Mesh::create(this, desc);

        if (!mesh.has_value()) {
            return std::nullopt;
        }

        return anim_mesh_pool_.store(std::move(mesh.value()));
    }

    auto createActorMesh(AnimatedMeshId mesh) -> std::optional<ActorMeshId> {
        auto actor_mesh = ActorMesh::create(this, mesh);

        if (!actor_mesh.has_value()) {
            return std::nullopt;
        }

        return actor_mesh_pool_.store(std::move(actor_mesh.value()));
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
            .vertex_buffer_flags = RendererBuffer::Usage::eNone,
            .index_buffer_flags = RendererBuffer::Usage::eNone,
        };

        auto mesh = Mesh::create(this, desc);

        if (!mesh.has_value()) {
            return std::nullopt;
        }

        return vector_mesh_pool_.store(std::move(mesh.value()));
    }

    template <std::invocable<const RendererTexture *> F> auto withTexture(TextureId handle, F consumer) const {
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

    auto deleteMesh(MeshId handle) { mesh_pool_.destroy(handle); }
    auto deleteTexture(TextureId handle) { texture_pool_->destroyResource(handle); }
    auto deleteAnimMesh(AnimatedMeshId handle) { anim_mesh_pool_.destroy(handle); }
    auto deleteActorMesh(ActorMeshId handle) { actor_mesh_pool_.destroy(handle); }
    auto deleteVectorMesh(VectorMeshId handle) { vector_mesh_pool_.destroy(handle); }

    auto getMesh(const MeshId &id) const -> const Mesh * { return mesh_pool_.get(id); }
    auto getTexture(const TextureId &id) const -> const RendererTexture * { return texture_pool_->getResource(id); }
    auto getAnimMesh(const AnimatedMeshId &id) const -> const Mesh * { return anim_mesh_pool_.get(id); }
    auto getActorMesh(const ActorMeshId &id) const -> const ActorMesh * { return actor_mesh_pool_.get(id); }
    auto getActorMesh(const ActorMeshId &id) -> ActorMesh * { return actor_mesh_pool_.get(id); }
    auto getVectorMesh(const VectorMeshId &id) const -> const Mesh * { return vector_mesh_pool_.get(id); }

    auto frame() -> util::Result;

    auto drawOpaqueMesh(OpaqueDrawDescription &&desc) -> util::Result { return draw_queue_.push(std::move(desc)); }

    auto drawSkinnedMesh(SkinnedDrawDescription &&desc) -> util::Result {
        return skinning_queue_.push(std::move(desc));
    }

    auto drawVectorMesh(VectorDrawDescription &&desc) -> util::Result { return vector_queue_.push(std::move(desc)); }

private:
    struct cbSkinningPushConstants {
        VkDeviceAddress bone_buffer;
        VkDeviceAddress input_buffer;
        VkDeviceAddress output_buffer;
    };

    struct cbOpaquePassPushConstants {
        VkDeviceAddress frame_heap;
        uint32_t object_id;
    };

    struct cbVectorPassPushConstants {
        VkDeviceAddress frame_heap;
        uint32_t object_id;
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

    struct PendingResize {
        VkExtent2D surface_extent, framebuffer_extent;
    };

    struct PerFrameData {
        std::optional<TextureId> geometry_target;
        std::optional<TextureId> vector_target_msaa;
        std::optional<TextureId> vector_target;
    };

    auto createSkinningPipeline(IShaderLoader &shader_loader) -> util::Result;
    auto createGeometryPipeline(IShaderLoader &shader_loader) -> util::Result;
    auto createVectorPipeline(IShaderLoader &shader_loader) -> util::Result;
    auto createLightingPipeline(IShaderLoader &shader_loader) -> util::Result;
    auto createInterfacePipeline(IShaderLoader &shader_loader) -> util::Result;

    auto createRenderTargets() -> void;

    struct IndexedDrawCall {
        VkBuffer vertex_buffer = VK_NULL_HANDLE;
        VkBuffer index_buffer = VK_NULL_HANDLE;
        uint32_t num_indices = 0;
    };

    using OpaqueIndexedDrawList = util::FixedSizeQueue<IndexedDrawCall, kNumMaxStaticObjects>;

    auto useTextureHandle(const RendererScheduler::FrameContext &context, std::optional<TextureId> handle) -> int32_t;

    auto prepareIndexedDraws(const RendererScheduler::FrameContext &context, OpaqueIndexedDrawList &draw_list) -> void;
    auto scheduleFrameWork(const RendererScheduler::FrameContext &context) -> void;

    Renderer() = default;

    Context *context_ = nullptr;
    RendererScheduler *scheduler_ = nullptr;

    Camera camera_;

    std::optional<PendingResize> pending_resize_;
    std::array<PerFrameData, RendererScheduler::kNumFramesInFlight> per_frame_data_;
    MutableSharedBuffer<cbFrameHeapBuffer> frame_heap_;
    MutableSharedBuffer<cbVectorHeapBuffer> vector_heap_;
    std::optional<TextureId> depth_buffer_;

    // resource pools
    std::unique_ptr<BindlessTexturePool<kNumTexturePoolSize>> texture_pool_;
    util::ManagedPool<Mesh, kNumMeshPoolSize, MeshTag> mesh_pool_;
    util::ManagedPool<Mesh, kNumAnimMeshPoolSize, AnimatedMeshTag> anim_mesh_pool_;
    util::ManagedPool<ActorMesh, kNumMaxSkinnedObjects, ActorMeshTag> actor_mesh_pool_;
    util::ManagedPool<Mesh, kNumMaxVectorMeshes, VectorMeshTag> vector_mesh_pool_;

    // render passes
    util::RefCountedPtr<ComputePipeline> skinning_pipeline_;
    util::RefCountedPtr<RenderPipeline> geometry_pipeline_;
    util::RefCountedPtr<RenderPipeline> vector_pipeline_;
    util::RefCountedPtr<RenderPipeline> lighting_pipeline_;
    util::RefCountedPtr<RenderPipeline> interface_pipeline_;

    // draw queue
    util::FixedSizeQueue<SkinnedDrawDescription, kNumMaxSkinnedObjects> skinning_queue_;
    util::FixedSizeQueue<OpaqueDrawDescription, kNumMaxStaticObjects> draw_queue_;
    util::FixedSizeQueue<VectorDrawDescription, kNumMaxVectorMeshes> vector_queue_;
};

} // namespace graphics