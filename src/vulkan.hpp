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

class Instance final {
public:
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

    friend class Instance;
};

}; // namespace graphics
