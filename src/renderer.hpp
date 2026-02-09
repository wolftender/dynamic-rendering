#pragma once
#include <array>
#include <deque>

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
private:
    struct TextureTag {};
    struct MeshTag {};

public:
    static constexpr uint32_t kNumFramesInFlight = 2;
    static constexpr uint32_t kNumMaxPointLights = 20;
    static constexpr uint32_t kNumMaxStaticObjects = 1024;
    static constexpr uint32_t kNumMaxSkinnedObjects = 128;
    static constexpr uint32_t kNumMaxBonesPerObject = 200;
    static constexpr uint32_t kNumTexturePoolSize = 256;
    static constexpr uint32_t kNumMeshPoolSize = 512;

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

    template <typename T> struct Identifier {
    private:
        uint32_t index_;
        explicit Identifier(uint32_t index) : index_{index} {}

    public:
        size_t id() const { return index_; }
        friend class Renderer;
    };

    using MeshId = Identifier<MeshTag>;
    using TextureId = Identifier<TextureTag>;

    struct Description {
        Context *context;
        std::unique_ptr<IShaderLoader> shader_loader;
    };

    struct OpaqueDrawDescription {
        MeshId mesh;
        glm::fmat4x4 world_matrix;
    };

    struct StaticVertex {
        glm::fvec3 position;
        glm::fvec3 normal;
        glm::fvec2 uv;
        glm::fvec3 color;
        glm::fvec4 tangent;
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

        Mesh(Mesh &&) noexcept = delete;
        auto operator=(Mesh &&) noexcept = delete;

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
            std::span<const uint32_t> indices) -> std::unique_ptr<Mesh>;

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

        Texture(Texture &&) noexcept = delete;
        auto operator=(Texture &&) noexcept = delete;

        ~Texture() noexcept;

        auto description() const -> const Description & { return description_; }
        auto image() const -> const Image & { return image_; }
        auto imageView() const -> const Image::View & { return image_view_; }
        auto sampler() const -> VkSampler { return sampler_; }

    private:
        static auto fromRgba(Renderer *renderer, const Description &desc, std::span<const uint8_t> rgba_data)
            -> std::unique_ptr<Texture>;

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

    template <util::TypedContiguousRange<StaticVertex> VR, util::TypedContiguousRange<const uint32_t> IR>
    auto createMesh(const VR &vertex_input_range, const IR &index_input_range) -> std::optional<MeshId> {
        const auto vertex_buffer_ptr = reinterpret_cast<const uint8_t *>(std::ranges::data(vertex_input_range));
        const auto vertex_buffer_size = std::ranges::size(vertex_input_range) * sizeof(StaticVertex);

        auto mesh = Mesh::create(
            this, {vertex_buffer_ptr, vertex_buffer_size}, std::ranges::size(vertex_input_range), index_input_range);

        auto id = mesh_pool_.insert(std::move(mesh));
        if (!id.has_value()) {
            return std::nullopt;
        }

        return MeshId{id.value()};
    }

    template <util::TypedContiguousRange<const uint8_t> R>
    auto createRgbaTexture(const Texture::Description &desc, const R &rgba_data) -> std::optional<TextureId> {
        const auto rgba_buffer_ptr = std::ranges::data(rgba_data);
        const auto rgba_buffer_size = std::ranges::size(rgba_data);

        auto texture = Texture::fromRgba(this, desc, {rgba_buffer_ptr, rgba_buffer_size});
        auto id = texture_pool_.insert(std::move(texture));

        if (!id.has_value()) {
            return std::nullopt;
        }

        return TextureId{id.value()};
    }

    template <std::invocable<const Texture &> F> auto withTexture(TextureId handle, F consumer) const {
        auto texture = texture_pool_.get(handle.id());
        if (!texture) {
            return;
        }

        consumer(*texture);
    }

    template <std::invocable<const Mesh &> F> auto withMesh(MeshId handle, F consumer) const {
        auto mesh = mesh_pool_.get(handle.id());
        if (!mesh) {
            return;
        }

        consumer(*mesh);
    }

    auto deleteMesh(MeshId handle) { mesh_pool_.erase(handle.id()); }
    auto deleteTexture(TextureId handle) { texture_pool_.erase(handle.id()); }

    auto frame() -> util::Result;

    auto drawOpaqueMesh(OpaqueDrawDescription &&desc) -> util::Result {
        if (draw_queue_fill_ == draw_queue_.size()) {
            return util::Result::eFailure;
        }

        draw_queue_[draw_queue_fill_++] = std::move(desc);
        return util::Result::eSuccess;
    }

private:
    class TexturePool final {
    public:
        TexturePool();
        ~TexturePool() noexcept;

        TexturePool(const TexturePool &) = delete;
        auto operator=(const TexturePool &) = delete;

        TexturePool(TexturePool &&) noexcept = delete;
        auto operator=(TexturePool &&) noexcept = delete;

        auto get(uint32_t handle) const -> const Texture *;
        auto insert(std::unique_ptr<Texture> texture) -> std::optional<uint32_t>;
        auto erase(uint32_t handle) -> std::unique_ptr<Texture>;

        auto numDescriptors() const -> uint32_t { return descriptors_.size(); }
        auto descriptors() const -> const VkDescriptorImageInfo * { return descriptors_.data(); }

    private:
        std::array<VkDescriptorImageInfo, kNumTexturePoolSize> descriptors_;
        std::array<std::unique_ptr<Texture>, kNumTexturePoolSize> textures_;
        std::deque<uint32_t> free_list_;
    };

    class MeshPool final {
    public:
        MeshPool();
        ~MeshPool() noexcept;

        MeshPool(const MeshPool &) = delete;
        auto operator=(const MeshPool &) = delete;

        MeshPool(MeshPool &&) noexcept = delete;
        auto operator=(MeshPool &&) noexcept = delete;

        auto get(uint32_t handle) const -> const Mesh *;
        auto insert(std::unique_ptr<Mesh> mesh) -> std::optional<uint32_t>;
        auto erase(uint32_t handle) -> std::unique_ptr<Mesh>;

    private:
        std::array<std::unique_ptr<Mesh>, kNumMeshPoolSize> meshes_;
        std::deque<uint32_t> free_list_;
    };

    // simple helper class to help us with our descriptor pools
    // it is bound to the descriptor set layout, so it only holds
    // enough descriptors for N layouts, where N is the number of
    // frames in flight
    class DescriptorSetHelper final {
    public:
        enum class DescriptorDataType {
            eShaderStorageBuffer,
            eUniformBuffer,
            eSamplerTexture,
        };

        struct DescriptorDescription {
            DescriptorDataType type;
            uint32_t num_bindings;
        };

        struct Description {
            std::vector<DescriptorDescription> layout;
        };

        static auto create(Renderer *renderer, const Description &description) -> std::unique_ptr<DescriptorSetHelper>;
        ~DescriptorSetHelper() noexcept;

        DescriptorSetHelper(const DescriptorSetHelper &) = delete;
        auto operator=(const DescriptorSetHelper &) = delete;

        DescriptorSetHelper(DescriptorSetHelper &&) noexcept = delete;
        auto operator=(DescriptorSetHelper &&) noexcept = delete;

        auto pool() const -> VkDescriptorPool { return pool_; }
        auto description() const -> const Description & { return desc_; }
        auto layout() const -> VkDescriptorSetLayout { return layout_; }
        auto getSetForFrame(uint32_t frame) const -> VkDescriptorSet { return sets_[frame]; }

    private:
        DescriptorSetHelper() = default;

        Renderer *renderer_ = nullptr;
        Description desc_;

        VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
        VkDescriptorPool pool_ = VK_NULL_HANDLE;

        std::array<VkDescriptorSet, kNumFramesInFlight> sets_;
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

    template <typename cbBufferDataType> class SceneBufferHelper;

    struct FrameData {
        std::unique_ptr<SceneBufferHelper<cbFrameHeapBuffer>> scene_buffer;
        VkCommandBuffer command_buffer = VK_NULL_HANDLE;

        VkFence fence = VK_NULL_HANDLE;
        VkSemaphore present_semaphore = VK_NULL_HANDLE;
    };

    auto getCurrentFrame() -> FrameData & { return frames_[current_frame_]; }

    Renderer() = default;

    Context *context_ = nullptr;
    Camera camera_;

    Image depth_buffer_;
    Image::View depth_buffer_view_;

    struct SwapchainImageData {
        VkSemaphore render_semaphore = VK_NULL_HANDLE;
    };

    VkCommandPool command_pool_ = VK_NULL_HANDLE;

    std::array<FrameData, kNumFramesInFlight> frames_;
    std::vector<SwapchainImageData> swapchain_data_;

    TexturePool texture_pool_;
    MeshPool mesh_pool_;

    // render passes
    std::unique_ptr<DescriptorSetHelper> descriptor_helper_ = nullptr;
    std::unique_ptr<OpaqueGeometryPass> geometry_pass_ = nullptr;

    // draw queue
    std::array<std::optional<OpaqueDrawDescription>, kNumMaxStaticObjects> draw_queue_;
    uint32_t draw_queue_fill_ = 0;
    uint32_t current_frame_ = 0;
};

} // namespace graphics