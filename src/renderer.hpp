#pragma once
#include "vulkan.hpp"

namespace graphics {

class Renderer final {
private:
    struct TextureTag {};
    struct MeshTag {};

public:
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

    static constexpr uint32_t kNumFramesInFlight = 2;
    static constexpr uint32_t kNumMaxPointLights = 20;
    static constexpr uint32_t kNumMaxStaticObjects = 1024;
    static constexpr uint32_t kNumMaxSkinnedObjects = 128;
    static constexpr uint32_t kNumMaxBonesPerObject = 200;
    static constexpr uint32_t kNumTexturePoolSize = 256;
    static constexpr uint32_t kNumMeshPoolSize = 512;

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

    struct StaticVertex {
        glm::fvec3 position;
        glm::fvec3 normal;
        glm::fvec2 uv;
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
            std::span<uint32_t> indices) -> std::unique_ptr<Mesh>;

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

    template <VertexType V, util::TypedContiguousRange<V> VR, util::TypedContiguousRange<uint32_t> IR>
    auto createMesh(const VR &vertex_input_range, const IR &index_input_range) -> MeshId {
        const auto vertex_buffer_ptr = reinterpret_cast<const uint8_t *>(std::ranges::data(vertex_input_range));
        const auto vertex_buffer_size = std::ranges::size(vertex_input_range) * sizeof(V);

        auto mesh = Mesh::create(
            this, {vertex_buffer_ptr, vertex_buffer_size}, std::ranges::size(vertex_input_range), index_input_range);

        auto id = mesh_pool_.insert(std::move(mesh));
        return MeshId{id};
    }

    template <util::TypedContiguousRange<const uint8_t> R>
    auto createRgbaTexture(const Texture::Description &desc, const R &rgba_data) -> TextureId {
        const auto rgba_buffer_ptr = std::ranges::data(rgba_data);
        const auto rgba_buffer_size = std::ranges::size(rgba_data);

        auto texture = Texture::fromRgba(this, desc, {rgba_buffer_ptr, rgba_buffer_size});
        auto id = texture_pool_.insert(std::move(texture));

        return TextureId{id};
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

    static auto create(const Description &description) -> std::unique_ptr<Renderer>;

    ~Renderer() noexcept;

    Renderer(const Renderer &) = delete;
    auto operator=(const Renderer &) = delete;

    Renderer(Renderer &&) noexcept = delete;
    auto operator=(Renderer &&) noexcept = delete;

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

        auto getPool() const -> VkDescriptorPool { return pool_; }
        auto getDescription() const -> const Description & { return desc_; }
        auto getSetForFrame(uint32_t frame) const -> VkDescriptorSet { return sets_[frame]; }

    private:
        DescriptorSetHelper() = default;

        Renderer *renderer_ = nullptr;
        Description desc_;

        VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
        VkDescriptorPool pool_ = VK_NULL_HANDLE;

        std::array<VkDescriptorSet, kNumFramesInFlight> sets_;
    };

    Renderer() = default;

    Context *context_ = nullptr;

    Image depth_buffer_;
    Image::View depth_buffer_view_;

    template <typename cbBufferDataType> class SceneBufferHelper;

    struct FrameData {
        std::unique_ptr<SceneBufferHelper<cbFrameHeapBuffer>> scene_buffer;
        VkCommandBuffer command_buffer = VK_NULL_HANDLE;

        VkFence fence = VK_NULL_HANDLE;
        VkSemaphore present_semaphore = VK_NULL_HANDLE;
    };

    struct SwapchainImageData {
        VkSemaphore render_semaphore = VK_NULL_HANDLE;
    };

    VkCommandPool command_pool_ = VK_NULL_HANDLE;

    std::array<FrameData, kNumFramesInFlight> frames_;
    std::vector<SwapchainImageData> swapchain_data_;

    TexturePool texture_pool_;
    MeshPool mesh_pool_;

    std::unique_ptr<DescriptorSetHelper> geometry_pass_descriptors_;
    VkShaderModule geometry_pass_shader_ = VK_NULL_HANDLE;

    uint32_t current_frame_;
};

} // namespace graphics