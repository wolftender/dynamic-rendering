#pragma once
#include <array>
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
public:
    static constexpr uint32_t kNumFramesInFlight = 2;
    static constexpr uint32_t kNumMaxPointLights = 20;
    static constexpr uint32_t kNumMaxStaticObjects = 2048;
    static constexpr uint32_t kNumMaxSkinnedObjects = 128;
    static constexpr uint32_t kNumMaxBonesPerObject = 200;

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

    template <VertexType V, util::TypedContiguousRange<V> VR, util::TypedContiguousRange<uint32_t> IR>
    auto createMesh(const VR &vertex_input_range, const IR &index_input_range) -> std::unique_ptr<Mesh> {
        const auto vertex_buffer_ptr = reinterpret_cast<const uint8_t *>(std::ranges::data(vertex_input_range));
        const auto vertex_buffer_size = std::ranges::size(vertex_input_range) * sizeof(V);

        return Mesh::create(
            this, {vertex_buffer_ptr, vertex_buffer_size}, std::ranges::size(vertex_input_range), index_input_range);
    }

    static auto create(const Description &description) -> std::unique_ptr<Renderer>;

    ~Renderer() noexcept;

    Renderer(const Renderer &) = delete;
    auto operator=(const Renderer &) = delete;

    Renderer(Renderer &&) noexcept = delete;
    auto operator=(Renderer &&) noexcept = delete;

private:
    Renderer() = default;

    Context *context_ = nullptr;

    Image depth_buffer_;
    Image::View depth_buffer_view_;

    template <typename cbBufferDataType> class SceneBufferHelper;

    struct FrameData {
        std::unique_ptr<SceneBufferHelper<cbSceneHeapBuffer>> scene_buffer;
        VkCommandBuffer command_buffer;
    };

    VkCommandPool command_pool_;
    std::array<FrameData, kNumFramesInFlight> frames_;
    uint32_t current_frame_;
};

}; // namespace graphics
