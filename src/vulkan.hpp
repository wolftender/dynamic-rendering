#pragma once
#include <array>
#include <deque>
#include <memory>
#include <vector>
#include <span>

#include <glm/glm.hpp>

#include <vulkan/vulkan.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vk_mem_alloc.h>

#include "util.hpp"

namespace graphics {

#define VK_CHECK_ERROR(vk_call)                                                                                        \
    do {                                                                                                               \
        const auto result = (vk_call);                                                                                 \
        if (VK_SUCCESS != result) {                                                                                    \
            LogError("vulkan error [{}]: in call {}", string_VkResult(result), #vk_call);                              \
            util::reportFatalError(fmt::format("vulkan error [{}]: in call {}", string_VkResult(result), #vk_call));   \
            exit(EXIT_FAILURE);                                                                                        \
        }                                                                                                              \
    } while (false);

#define VK_PROPAGATE_ERROR(vk_call)                                                                                    \
    do {                                                                                                               \
        const auto result = (vk_call);                                                                                 \
        if (VK_SUCCESS != result) {                                                                                    \
            LogError("vulkan error [{}]: in call {}", string_VkResult(result), #vk_call);                              \
            return result                                                                                              \
        }                                                                                                              \
    } while (false);

template <typename T>
concept VertexType = requires() {
    { T::layout() } -> std::convertible_to<std::span<VkVertexInputAttributeDescription>>;
};

class Buffer final {
public:
    Buffer() = default;
    ~Buffer() noexcept;

    Buffer(const Buffer &) = delete;
    auto operator=(const Buffer &) = delete;

    Buffer(Buffer &&buffer) noexcept;
    auto operator=(Buffer &&buffer) noexcept -> Buffer &;

    auto buffer() const -> VkBuffer { return buffer_; }
    auto addrOf() const -> const VkBuffer * { return &buffer_; }

    auto memPropFlags() const -> VkMemoryPropertyFlags;
    auto flush(VkDeviceSize offset = 0ull, VkDeviceSize size = VK_WHOLE_SIZE) const;

    auto cpuMappedPointer() const -> void * { return allocation_info_.pMappedData; }

private:
    auto destroy() noexcept -> void;

    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;

    VkBuffer buffer_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VmaAllocationInfo allocation_info_ = {};

    friend class Context;
};

class Image final {
public:
    class View final {
    public:
        View() = default;
        ~View() noexcept;

        View(const View &) = delete;
        auto operator=(const View &) = delete;

        View(View &&view) noexcept;
        auto operator=(View &&view) noexcept -> View &;

        auto view() const -> VkImageView { return image_view_; }
        auto image() const -> const Image * { return image_; }

    private:
        auto destroy() noexcept -> void;

        const Image *image_ = nullptr;
        VkDevice device_ = VK_NULL_HANDLE;
        VkImageView image_view_ = VK_NULL_HANDLE;

        friend class Image;
    };

    Image() = default;
    ~Image() noexcept;

    Image(const Image &image) = delete;
    auto operator=(const Image &image) = delete;

    Image(Image &&image) noexcept;
    auto operator=(Image &&image) noexcept -> Image &;

    auto image() const -> VkImage { return image_; }
    auto addrOf() const -> const VkImage * { return &image_; }
    auto format() const -> VkFormat { return format_; }

    auto createView(VkImageViewType type, VkFormat format, VkImageAspectFlags aspect_flags) const -> View;
    auto memPropFlags() const -> VkMemoryPropertyFlags;
    auto flush(VkDeviceSize offset = 0ull, VkDeviceSize size = VK_WHOLE_SIZE) const;

private:
    auto destroy() noexcept -> void;

    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;

    VkImage image_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VmaAllocationInfo allocation_info_ = {};

    VkFormat format_;

    friend class View;
    friend class Context;
};

class Context final {
public:
    struct Description {
        VkSurfaceKHR surface;
        VkExtent2D surface_extent;
        VkExtent2D framebuffer_extent;
    };

    class MemoryHelper final {
    public:
        ~MemoryHelper() noexcept;

        MemoryHelper(const MemoryHelper &&) = delete;
        auto operator=(const MemoryHelper &&) = delete;

        MemoryHelper(MemoryHelper &&) noexcept = delete;
        auto operator=(MemoryHelper &&) noexcept = delete;

        auto createBuffer(VkBufferUsageFlags usage, std::span<const uint8_t> data) const -> Buffer;
        auto createStagingBuffer(VkDeviceSize size) const -> Buffer;
        auto createDeviceBuffer(VkBufferUsageFlags usage, VkDeviceSize size) const -> Buffer;
        auto createSharedBuffer(VkBufferUsageFlags usage, VkDeviceSize size) const -> Buffer;

        auto createImage(const VkImageCreateInfo &image_info) const -> Image;
        auto createImage(VkFormat format, VkImageUsageFlags usage, VkImageType type, const VkExtent3D &extent) const
            -> Image;
        auto createImageRgba(VkImageUsageFlags usage, VkExtent2D extent, std::span<const uint8_t> data) const -> Image;

        template <std::invocable<VkCommandBuffer> F> auto runOnTransferQueue(F runner) const {
            beginCommandBuffer();
            runner(command_buffer_);
            submitCommandBuffer(upload_semaphore_, ++upload_timeline_);
        }

    private:
        static auto create(Context *context) -> std::unique_ptr<MemoryHelper>;

        MemoryHelper() = default;

        auto beginCommandBuffer() const -> void;
        auto submitCommandBuffer(VkSemaphore semaphore, uint64_t signal_value) const -> void;

        Context *context_ = nullptr;

        mutable uint64_t upload_timeline_ = 0ull;
        VkSemaphore upload_semaphore_ = VK_NULL_HANDLE;
        VkCommandPool command_pool_ = VK_NULL_HANDLE;
        VkCommandBuffer command_buffer_ = VK_NULL_HANDLE;

        friend class Context;
    };

    ~Context() noexcept;

    Context(const Context &) = delete;
    auto operator=(const Context &) = delete;

    Context(Context &&) noexcept = delete;
    auto operator=(Context &&) noexcept = delete;

    auto resize(VkExtent2D surface_extent, VkExtent2D framebuffer_extent) -> void;

    auto instance() const -> VkInstance { return instance_; }
    auto surface() const -> VkSurfaceKHR { return surface_; }
    auto physicalDevice() const -> VkPhysicalDevice { return physical_device_; }
    auto physicalDeviceProperies() const -> const VkPhysicalDeviceProperties & { return phys_dev_props_; }
    auto device() const -> VkDevice { return device_; }
    auto presentQueue() const -> VkQueue { return present_queue_; }
    auto graphicsQueue() const -> VkQueue { return graphics_queue_; }
    auto swapchain() const -> VkSwapchainKHR { return swapchain_; }
    auto memory() -> MemoryHelper & { return *memory_; }
    auto memory() const -> const MemoryHelper & { return *memory_; }
    auto surfaceExtent() const -> const VkExtent2D & { return surface_extent_; }
    auto graphicsQueueFamily() const -> uint32_t { return graphics_queue_family_; }
    auto presentQueueFamily() const -> uint32_t { return present_queue_family_; }
    auto swapchainFormat() const -> VkSurfaceFormatKHR { return swapchain_format_; }
    auto presentMode() const -> VkPresentModeKHR { return present_mode_; }
    auto framebufferExtent() const -> const VkExtent2D & { return framebuffer_extent_; }
    auto swapchainImages() const -> const std::vector<VkImage> & { return swapchain_images_; }
    auto swapchainImageViews() const -> const std::vector<VkImageView> & { return swapchain_image_views_; }
    auto supportedDepthFormat() const -> VkFormat { return supported_depth_format_; }

private:
    static auto create(VkInstance instance, const Description &description) -> std::unique_ptr<Context>;

    Context() = default;

    auto createSwapchain() -> void;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties phys_dev_props_ = {};
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue present_queue_ = VK_NULL_HANDLE;
    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;

    VmaVulkanFunctions allocator_funcs_ = {};
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    std::unique_ptr<MemoryHelper> memory_;

    VkExtent2D surface_extent_ = {};
    uint32_t graphics_queue_family_ = 0;
    uint32_t present_queue_family_ = 0;

    VkSurfaceFormatKHR swapchain_format_ = {};
    VkPresentModeKHR present_mode_ = {};
    VkExtent2D framebuffer_extent_ = {};

    uint32_t num_swapchain_images_ = 0;
    std::vector<VkImage> swapchain_images_;
    std::vector<VkImageView> swapchain_image_views_;

    VkFormat supported_depth_format_ = {};

    VkFence immediate_fence_ = VK_NULL_HANDLE;
    VkCommandBuffer immediate_command_buffer_ = VK_NULL_HANDLE;
    VkCommandPool immediate_command_pool_ = VK_NULL_HANDLE;

    friend class Instance;
};

class Instance final {
public:
    struct Description {
        std::vector<const char *> instance_extensions = {};
    };

    auto static create(const Description &description) -> std::unique_ptr<Instance>;

    ~Instance() noexcept;

    Instance(const Instance &) = delete;
    auto operator=(const Instance &) = delete;

    Instance(Instance &&) noexcept = delete;
    auto operator=(Instance &&) noexcept = delete;

    auto instance() const -> VkInstance { return instance_; }
    auto createContext(const Context::Description &description) -> std::unique_ptr<Context>;

private:
    static bool s_initialized_loader_;
    static bool s_initialized_instance_loader_;
    static bool s_initialized_device_loader_;

    Instance() = default;

    bool enable_validation_layers_ = false;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger_ = VK_NULL_HANDLE;

    friend class Context;
};

class Renderer final {
private:
    struct TextureTag {};
    struct MeshTag {};

public:
    static constexpr uint32_t kNumFramesInFlight = 2;
    static constexpr uint32_t kNumMaxPointLights = 20;
    static constexpr uint32_t kNumMaxStaticObjects = 2048;
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
    };

    struct StaticVertex {
        glm::fvec3 position;
        glm::fvec3 normal;
        glm::fvec2 uv;
        glm::fvec4 tangent;
    };

    struct cbSceneHeapBuffer {
        struct cbPointLightData {
            glm::fvec4 world_position;
            glm::fvec4 color_intensity;
        };

        struct cbStaticObjectData {
            glm::fmat4x4 world;
        };

        struct cbSkinnedObjectData {
            glm::fmat4x4 world;
            std::array<glm::fmat4x4, kNumMaxBonesPerObject> bones;
        };

        glm::fmat4x4 projection;
        glm::fmat4x4 view;

        glm::fmat4x4 projection_inv;
        glm::fmat4x4 view_inv;

        glm::fvec4 ambient;
        std::array<cbPointLightData, kNumMaxPointLights> point_lights;
        std::array<cbStaticObjectData, kNumMaxStaticObjects> static_objects;
        std::array<cbSkinnedObjectData, kNumMaxSkinnedObjects> skinned_objects;

        uint32_t num_lights, num_static_objects, num_skinned_objects; // active
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

    Renderer() = default;

    Context *context_ = nullptr;

    Image depth_buffer_;
    Image::View depth_buffer_view_;

    template <typename cbBufferDataType> class SceneBufferHelper;

    struct FrameData {
        std::unique_ptr<SceneBufferHelper<cbSceneHeapBuffer>> scene_buffer;
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

    VkDescriptorSetLayout desc_set_layout_graphics_ = VK_NULL_HANDLE;
    VkDescriptorPool desc_pool_graphics_ = VK_NULL_HANDLE;
    VkDescriptorSet desc_set_graphics_ = VK_NULL_HANDLE;

    TexturePool texture_pool_;
    MeshPool mesh_pool_;

    uint32_t current_frame_;
};

}; // namespace graphics
