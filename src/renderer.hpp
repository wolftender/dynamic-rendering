#pragma once
#include <array>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>

#include "vulkan.hpp"

#undef near
#undef far

namespace graphics {

class Camera final {
public:
    Camera()
        : position_{0.0f, 0.0f, 1.0f}, target_{0.0f, 0.0f, 0.0f}, aspect_{1.0f}, fov_{glm::pi<float>() * 0.5f},
          near_{0.5f}, far_{200.0f}, dirty_bit_proj_{true}, dirty_bit_view_{true}, projection_{1.0f},
          projection_inv_{1.0f}, view_{1.0f}, view_inv_{1.0f} {}

    auto position() const -> const glm::fvec3 & { return position_; }
    auto target() const -> const glm::fvec3 & { return target_; }
    auto aspect() const -> float { return aspect_; }
    auto fov() const -> float { return fov_; }
    auto near() const -> float { return near_; }
    auto far() const -> float { return far_; }

    auto setPosition(const glm::fvec3 &position) -> void {
        position_ = position;
        dirty_bit_view_ = true;
    }

    auto setTarget(const glm::fvec3 &target) -> void {
        target_ = target;
        dirty_bit_view_ = true;
    }

    auto setAspect(float aspect) -> void {
        aspect_ = aspect;
        dirty_bit_proj_ = true;
    }

    auto setFov(float fov) -> void {
        fov_ = fov;
        dirty_bit_proj_ = true;
    }

    auto setNear(float near) -> void {
        near_ = near;
        dirty_bit_proj_ = true;
    }

    auto setFar(float far) -> void {
        far_ = far;
        dirty_bit_proj_ = true;
    }

    auto projection() const -> const glm::fmat4x4 & {
        calculateProjection();
        return projection_;
    }

    auto view() const -> const glm::fmat4x4 & {
        calculateView();
        return view_;
    }

    auto projectionInv() const -> const glm::fmat4x4 & {
        calculateProjection();
        return projection_inv_;
    }

    auto viewInv() const -> const glm::fmat4x4 & {
        calculateView();
        return view_inv_;
    }

private:
    inline auto calculateProjection() const -> void {
        if (dirty_bit_proj_) {
            projection_ = glm::perspective(fov_, aspect_, near_, far_);
            projection_inv_ = glm::inverse(projection_);
            dirty_bit_proj_ = false;
        }
    }

    inline auto calculateView() const -> void {
        if (dirty_bit_view_) {
            view_ = glm::lookAt(position_, target_, glm::fvec3{0.0f, 1.0f, 0.0f});
            view_inv_ = glm::inverse(view_);
            dirty_bit_view_ = false;
        }
    }

    glm::fvec3 position_;
    glm::fvec3 target_;

    float aspect_, fov_, near_, far_;

    // cache
    mutable bool dirty_bit_proj_;
    mutable bool dirty_bit_view_;
    mutable glm::fmat4x4 projection_, projection_inv_;
    mutable glm::fmat4x4 view_, view_inv_;
};

class Renderer final {
public:
    static constexpr uint32_t kNumFramesInFlight = 2;
    static constexpr uint32_t kNumMaxPointLights = 20;
    static constexpr uint32_t kNumMaxStaticObjects = 1024;
    static constexpr uint32_t kNumMaxSkinnedObjects = 128;
    static constexpr uint32_t kNumMaxBonesPerObject = 200;
    static constexpr uint32_t kNumTexturePoolSize = 256;
    static constexpr uint32_t kNumMeshPoolSize = 512;

    class Mesh;
    class Texture;

    class IShaderLoader {
    public:
        IShaderLoader() = default;

        virtual ~IShaderLoader() = default;
        virtual auto loadGeometryPassShader() const -> std::optional<std::vector<uint32_t>> = 0;

        IShaderLoader(const IShaderLoader &) = delete;
        auto operator=(const IShaderLoader &) = delete;

        IShaderLoader(IShaderLoader &&) noexcept = delete;
        auto operator=(IShaderLoader &&) noexcept = delete;
    };

    template <typename T> class ResourceId final {
    public:
        ResourceId(uint32_t index, uint32_t generation) : index_{index}, generation_{generation} {}

        auto index() const -> uint32_t { return index_; }
        auto generation() const -> uint32_t { return generation_; }

    private:
        uint32_t index_;
        uint32_t generation_;
    };

    using MeshId = ResourceId<Mesh>;
    using TextureId = ResourceId<Texture>;

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

    struct StaticVertex {
        glm::fvec3 position;
        glm::fvec3 normal;
        glm::fvec2 uv;
        glm::fvec3 color;
        glm::fvec4 tangent;

        StaticVertex() = default;

        StaticVertex(
            const glm::fvec3 &pos, const glm::fvec3 &norm, const glm::fvec2 &uv_coords, const glm::fvec3 &col,
            const glm::fvec4 &tan)
            : position{pos}, normal{norm}, uv{uv_coords}, color{col}, tangent{tan} {}

        StaticVertex(const glm::fvec3 &pos, const glm::fvec3 &norm, const glm::fvec2 &uv_coords, const glm::fvec4 &tan)
            : position{pos}, normal{norm}, uv{uv_coords}, color{1.0f, 1.0f, 1.0f}, tangent{tan} {}

        StaticVertex(
            float x, float y, float z, float nx, float ny, float nz, float u, float v, float tx, float ty, float tz,
            float tw)
            : position{x, y, z}, normal{nx, ny, nz}, uv{u, v}, color{1.0f, 1.0f, 1.0f}, tangent{tx, ty, tz, tw} {}

        StaticVertex(
            float x, float y, float z, float nx, float ny, float nz, float u, float v, float r, float g, float b,
            float tx, float ty, float tz, float tw)
            : position{x, y, z}, normal{nx, ny, nz}, uv{u, v}, color{r, g, b}, tangent{tx, ty, tz, tw} {}
    };

    enum PerFrameDescriptors : uint32_t {
        PerFrameTexturePool = 0,
    };

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

    class Mesh final {
    public:
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
        static auto create(
            Renderer *renderer, std::span<const uint8_t> vertex_buffer, uint32_t num_vertices,
            std::span<const uint32_t> indices) -> std::optional<Mesh>;

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

        auto mesh = Mesh::create(
            this, {vertex_buffer_ptr, vertex_buffer_size}, std::ranges::size(vertex_input_range), index_input_range);

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

    auto deleteMesh(MeshId handle) { unrefMesh(handle); }
    auto deleteTexture(TextureId handle) { unrefTexture(handle); }
    auto getMesh(const MeshId &id) const -> const Mesh *;
    auto getTexture(const TextureId &id) const -> const Texture *;

    auto frame() -> util::Result;

    auto drawOpaqueMesh(OpaqueDrawDescription &&desc) -> util::Result {
        if (draw_queue_fill_ == draw_queue_.size()) {
            return util::Result::eFailure;
        }

        draw_queue_[draw_queue_fill_++] = std::move(desc);
        return util::Result::eSuccess;
    }

private:
    template <typename T, uint32_t kPoolSize> class ResourcePool;
    class BindlessTexturePool;

    // simple helper class to help us with our descriptor pools
    // it is bound to the descriptor set layout, so it only holds
    // enough descriptors for N layouts, where N is the number of
    // frames in flight
    template <uint32_t kNumSets = 1ull> class DescriptorSetHelper;

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

    template <typename cbBufferDataType> class SceneBufferHelper;

    struct FrameData {
        std::unique_ptr<SceneBufferHelper<cbFrameHeapBuffer>> scene_buffer;
        VkCommandBuffer command_buffer = VK_NULL_HANDLE;

        VkFence fence = VK_NULL_HANDLE;
        VkSemaphore present_semaphore = VK_NULL_HANDLE;
    };

    auto addMesh(Mesh &&mesh) -> std::optional<MeshId>;
    auto addTexture(Texture &&texture) -> std::optional<TextureId>;
    auto unrefMesh(const MeshId &id) -> void;
    auto unrefTexture(const TextureId &id) -> void;

    auto createSwapchainData() -> void;
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
    std::unique_ptr<ResourcePool<Mesh, kNumMeshPoolSize>> mesh_pool_;

    // render passes
    // std::unique_ptr<DescriptorSetHelper> descriptor_helper_ = nullptr;
    std::unique_ptr<OpaqueGeometryPass> geometry_pass_ = nullptr;

    // draw queue
    std::array<std::optional<OpaqueDrawDescription>, kNumMaxStaticObjects> draw_queue_;
    uint32_t draw_queue_fill_ = 0;
    uint32_t current_frame_ = 0;
};

} // namespace graphics