#pragma once
#include <memory>
#include <vector>

#include <vulkan/vulkan.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vk_mem_alloc.h>

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

class Buffer final {
public:
    Buffer() = default;
    ~Buffer() noexcept;

    Buffer(const Buffer &) = delete;
    auto operator=(const Buffer &) = delete;

    Buffer(Buffer &&buffer) noexcept;
    auto operator=(Buffer &&buffer) noexcept -> Buffer &;

    auto buffer() const -> VkBuffer { return buffer_; }
    auto addr_of() const -> const VkBuffer * { return &buffer_; }

    auto mem_prop_flags() const -> VkMemoryPropertyFlags;
    auto flush(VkDeviceSize offset = 0ull, VkDeviceSize size = VK_WHOLE_SIZE) const;

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
    auto addr_of() const -> const VkImage * { return &image_; }

    auto create_view(VkImageViewType type, VkFormat format, VkImageAspectFlags aspect_flags) const -> View;
    auto mem_prop_flags() const -> VkMemoryPropertyFlags;
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

    ~Context() noexcept;

    Context(const Context &) = delete;
    auto operator=(const Context &) = delete;

    Context(Context &&) noexcept = delete;
    auto operator=(Context &&) noexcept = delete;

    auto resize(VkExtent2D surface_extent, VkExtent2D framebuffer_extent) -> void;

    auto instance() const -> VkInstance { return instance_; }
    auto surface() const -> VkSurfaceKHR { return surface_; }
    auto physical_device() const -> VkPhysicalDevice { return physical_device_; }
    auto device() const -> VkDevice { return device_; }
    auto present_queue() const -> VkQueue { return present_queue_; }
    auto graphics_queue() const -> VkQueue { return graphics_queue_; }
    auto swapchain() const -> VkSwapchainKHR { return swapchain_; }
    auto surface_extent() const -> const VkExtent2D & { return surface_extent_; }
    auto graphics_queue_family() const -> uint32_t { return graphics_queue_family_; }
    auto present_queue_family() const -> uint32_t { return present_queue_family_; }
    auto swapchain_format() const -> VkSurfaceFormatKHR { return swapchain_format_; }
    auto present_mode() const -> VkPresentModeKHR { return present_mode_; }
    auto framebuffer_extent() const -> const VkExtent2D & { return framebuffer_extent_; }
    auto swapchain_images() const -> const std::vector<VkImage> & { return swapchain_images_; }
    auto swapchain_image_views() const -> const std::vector<VkImageView> & { return swapchain_image_views_; }

    auto create_image(const VkImageCreateInfo &image_info) -> Image;
    auto create_image(VkFormat format, VkImageUsageFlags usage, VkImageType type, const VkExtent3D &extent) -> Image;

private:
    static auto create(VkInstance instance, const Description &description) -> std::unique_ptr<Context>;

    Context() = default;

    auto create_swapchain() -> void;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue present_queue_ = VK_NULL_HANDLE;
    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;

    VmaVulkanFunctions allocator_funcs_ = {};
    VmaAllocator allocator_ = VK_NULL_HANDLE;

    VkExtent2D surface_extent_ = {};
    uint32_t graphics_queue_family_ = 0;
    uint32_t present_queue_family_ = 0;

    VkSurfaceFormatKHR swapchain_format_ = {};
    VkPresentModeKHR present_mode_ = {};
    VkExtent2D framebuffer_extent_ = {};

    uint32_t num_swapchain_images_ = 0;
    std::vector<VkImage> swapchain_images_;
    std::vector<VkImageView> swapchain_image_views_;

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
    auto create_context(const Context::Description &description) -> std::unique_ptr<Context>;

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
    struct Description {
        Context *context;
    };

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
};

}; // namespace graphics
