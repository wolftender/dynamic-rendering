#pragma once
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include <vulkan/vulkan.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vk_mem_alloc.h>

#include "memory.hpp"

namespace graphics {

class MemoryHelper;

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
    auto getMaxSampleCount() const -> VkSampleCountFlagBits;
    auto chooseBestSampleCount(uint32_t samples) const -> VkSampleCountFlagBits;

private:
    static auto create(VkInstance instance, const Description &description) -> std::unique_ptr<Context>;

    Context() = default;

    auto createSwapchain() -> void;
    auto rebuildSwapchain() -> void;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties phys_dev_props_ = {};
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue present_queue_ = VK_NULL_HANDLE;
    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;

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

}; // namespace graphics
