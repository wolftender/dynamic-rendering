#include <limits>

#include "vulkan.hpp"
#include "logger.hpp"
#include "util.hpp"

#include <vulkan/vulkan.h>
#include <volk.h>
#include <vk_mem_alloc.h>
#include <vulkan/vk_enum_string_helper.h>

namespace graphics {

constexpr const std::string_view kApplicationName = "dynamic rendering";
constexpr const char *kValidationLayerName = "VK_LAYER_KHRONOS_validation";

constexpr std::array<const char *, 2> kRequiredDeviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME};

#ifdef NDEBUG
constexpr bool kEnableValidationLayers = false;
#else
constexpr bool kEnableValidationLayers = true;
#endif

auto messageSeverityToString(VkDebugUtilsMessageSeverityFlagBitsEXT s) -> std::string_view {
    switch (s) {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
        return "VERBOSE";
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
        return "ERROR";
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
        return "WARNING";
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
        return "INFO";
    default:
        return "UNKNOWN";
    }
}

auto messageTypeToString(VkDebugUtilsMessageTypeFlagsEXT s) -> std::string_view {
    switch (static_cast<uint32_t>(s)) {
    case 7:
        return "General | Validation | Performance";
    case 6:
        return "Validation | Performance";
    case 5:
        return "General | Performance";
    case 4:
        return "Performance";
    case 3:
        return "General | Validation";
    case 2:
        return "Validation";
    case 1:
        return "General";
    default:
        return "Unknown";
    }
}

auto physicalDeviceTypeToString(const VkPhysicalDeviceType &type) -> std::string_view {
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return "discrete gpu";
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return "integrated gpu";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        return "virtual gpu";
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        return "cpu";
    default:
        return "other";
    }
}

static VKAPI_ATTR auto VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT *p_cb_data, [[maybe_unused]] void *p_user_data) -> VkBool32 {
    LogError(
        "[vulkan validation][{}: {}] {}", messageSeverityToString(severity), messageTypeToString(type),
        p_cb_data->pMessage);

    return VK_FALSE;
}

auto checkInstanceLayerSupport(std::string_view layer_name) -> bool {
    static std::vector<VkLayerProperties> s_properties = []() {
        uint32_t num_layers = 0;
        vkEnumerateInstanceLayerProperties(&num_layers, nullptr);

        std::vector<VkLayerProperties> properties{num_layers};
        vkEnumerateInstanceLayerProperties(&num_layers, properties.data());

        return properties;
    }();

    for (const auto &property : s_properties) {
        if (std::string_view{property.layerName} == layer_name) {
            return true;
        }
    }

    return false;
}

auto checkIfEnableValidationLayers() -> bool {
    if (!kEnableValidationLayers) {
        return false;
    }

    return checkInstanceLayerSupport(kValidationLayerName);
}

bool Instance::s_initialized_loader_ = false;
bool Instance::s_initialized_instance_loader_ = false;
bool Instance::s_initialized_device_loader_ = false;

auto Instance::create(const Description &description) -> std::unique_ptr<Instance> {
    if (!s_initialized_loader_) {
        LogInfo("vulkan: volk not initialized yet, load function pointers");

        volkInitialize();
        s_initialized_loader_ = true;
    }

    std::unique_ptr<Instance> context{new (std::nothrow) Instance()};
    if (!context) {
        LogError("cannot allocate new context object");
        util::reportFatalError("cannot allocate new context object");

        return nullptr;
    }

    // check if we should enable validation layers
    context->enable_validation_layers_ = checkIfEnableValidationLayers();

    // instance extensions + layers, some are conditional
    std::vector<const char *> instance_extensions = description.instance_extensions;
    if (context->enable_validation_layers_) {
        instance_extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    std::vector<const char *> instance_layers = {};
    if (context->enable_validation_layers_) {
        instance_layers.emplace_back(kValidationLayerName);
    }

    VkDebugUtilsMessengerCreateInfoEXT debug_msg_info = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .pNext = nullptr,
        .flags = 0,
        .messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debugCallback,
        .pUserData = nullptr,
    };

    VkApplicationInfo application_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = kApplicationName.data(),
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "no engine",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_3,
    };

    VkInstanceCreateInfo instance_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = context->enable_validation_layers_ ? &debug_msg_info : nullptr,
        .flags = 0,
        .pApplicationInfo = &application_info,
        .enabledLayerCount = static_cast<uint32_t>(instance_layers.size()),
        .ppEnabledLayerNames = instance_layers.size() > 0 ? instance_layers.data() : nullptr,
        .enabledExtensionCount = static_cast<uint32_t>(instance_extensions.size()),
        .ppEnabledExtensionNames = instance_extensions.size() > 0 ? instance_extensions.data() : nullptr,
    };

    LogInfo("vulkan: create instance");
    VK_CHECK_ERROR(vkCreateInstance(&instance_info, nullptr, &context->instance_));
    if (!s_initialized_instance_loader_) {
        LogInfo("vulkan: volk not initialized yet, load instance function pointers");

        volkLoadInstance(context->instance_);
        s_initialized_instance_loader_ = true;
    }

    if (context->enable_validation_layers_) {
        LogInfo("vulkan: enabled validation layers, create debug messenger");
        VK_CHECK_ERROR(
            vkCreateDebugUtilsMessengerEXT(context->instance_, &debug_msg_info, nullptr, &context->debug_messenger_));
    }

    return context;
}

auto Instance::create_context(const Context::Description &description) -> std::unique_ptr<Context> {
    return Context::create(instance_, description);
}

struct QueueFamiliesIndices {
    uint32_t graphics, present;
};

auto getCompatibleDevices(VkInstance instance, VkSurfaceKHR surface)
    -> std::vector<std::pair<VkPhysicalDevice, QueueFamiliesIndices>> {
    uint32_t num_physical_devices = 0;
    VK_CHECK_ERROR(vkEnumeratePhysicalDevices(instance, &num_physical_devices, nullptr));
    std::vector<VkPhysicalDevice> physical_devices{num_physical_devices};
    VK_CHECK_ERROR(vkEnumeratePhysicalDevices(instance, &num_physical_devices, physical_devices.data()));

    std::vector<std::pair<VkPhysicalDevice, QueueFamiliesIndices>> compatible_devices;
    for (auto &&physical_device : physical_devices) {
        VkPhysicalDeviceProperties device_properties = {};
        vkGetPhysicalDeviceProperties(physical_device, &device_properties);

        if (device_properties.apiVersion < VK_VERSION_1_3) {
            LogWarning(
                "vulkan: unsupported device {}: {} ({}), reason: vulkan version too low", device_properties.deviceID,
                device_properties.deviceName, physicalDeviceTypeToString(device_properties.deviceType));
            continue;
        }

        VkPhysicalDeviceVulkan11Features device_features_11 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
            .pNext = nullptr,
        };

        VkPhysicalDeviceVulkan12Features device_features_12 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
            .pNext = &device_features_11,
        };

        VkPhysicalDeviceVulkan13Features device_features_13 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
            .pNext = &device_features_12,
        };

        VkPhysicalDeviceFeatures2 device_features = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &device_features_13,
        };

        vkGetPhysicalDeviceFeatures2(physical_device, &device_features);
        if (!device_features_12.bufferDeviceAddress || !device_features_12.descriptorIndexing ||
            !device_features_12.descriptorBindingVariableDescriptorCount ||
            !device_features_12.runtimeDescriptorArray || !device_features_13.synchronization2 ||
            !device_features_13.dynamicRendering) {
            LogWarning(
                "vulkan: unsupported device {}: {} ({}), reason: missing required vulkan 1.2 and 1.3 features",
                device_properties.deviceID, device_properties.deviceName,
                physicalDeviceTypeToString(device_properties.deviceType));
            continue;
        }

        uint32_t num_queue_families = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &num_queue_families, nullptr);
        std::vector<VkQueueFamilyProperties> queue_family_properties{num_queue_families};
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &num_queue_families, queue_family_properties.data());

        std::optional<uint32_t> graphics_family;
        std::optional<uint32_t> present_family;

        for (uint32_t index = 0; index < num_queue_families; ++index) {
            VkBool32 is_graphics = (queue_family_properties[index].queueFlags & VK_QUEUE_GRAPHICS_BIT);
            VkBool32 is_present = VK_FALSE;

            VK_CHECK_ERROR(vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, index, surface, &is_present));

            if (is_graphics && is_present) {
                graphics_family = index;
                present_family = index;
                break;
            } else if (is_graphics && !graphics_family.has_value()) {
                graphics_family = index;
            } else if (is_present && !present_family.has_value()) {
                present_family = index;
            }
        }

        if (!graphics_family.has_value() || !present_family.has_value()) {
            LogWarning(
                "vulkan: unsupported device {}: {} ({}), reason: cannot create required queue types",
                device_properties.deviceID, device_properties.deviceName,
                physicalDeviceTypeToString(device_properties.deviceType));
            continue;
        }

        uint32_t num_supported_extensions = 0;
        VK_CHECK_ERROR(
            vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &num_supported_extensions, nullptr));
        std::vector<VkExtensionProperties> supported_extensions{num_supported_extensions};
        VK_CHECK_ERROR(vkEnumerateDeviceExtensionProperties(
            physical_device, nullptr, &num_supported_extensions, supported_extensions.data()));

        bool supports_all_extensions = true;
        for (const auto &required_extension : kRequiredDeviceExtensions) {
            if (supported_extensions.end() == std::find_if(
                                                  supported_extensions.begin(), supported_extensions.end(),
                                                  [&](const VkExtensionProperties &ext_properties) -> bool {
                return 0 == strcmp(ext_properties.extensionName, required_extension);
            })) {
                LogWarning(
                    "vulkan: unsupported device {}: {} ({}), reason: missing support for {}",
                    device_properties.deviceID, device_properties.deviceName,
                    physicalDeviceTypeToString(device_properties.deviceType), required_extension);
                supports_all_extensions = false;
            }
        }

        if (!supports_all_extensions) {
            continue;
        }

        uint32_t num_surface_formats = 0, num_present_modes = 0;
        VK_CHECK_ERROR(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &num_surface_formats, nullptr));
        VK_CHECK_ERROR(
            vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &num_present_modes, nullptr));

        if (num_surface_formats == 0 || num_present_modes == 0) {
            LogWarning(
                "vulkan: unsupported device {}: {} ({}), reason: no present modes or surface formats",
                device_properties.deviceID, device_properties.deviceName,
                physicalDeviceTypeToString(device_properties.deviceType));
            continue;
        }

        LogInfo(
            "vulkan: found supported device {}: {} ({})", device_properties.deviceID, device_properties.deviceName,
            physicalDeviceTypeToString(device_properties.deviceType));

        compatible_devices.push_back(
            std::make_pair(physical_device, QueueFamiliesIndices{graphics_family.value(), present_family.value()}));
    }

    return compatible_devices;
}

auto Instance::Context::create(VkInstance instance, const Description &description) -> std::unique_ptr<Context> {
    std::unique_ptr<Context> context{new (std::nothrow) Context()};
    if (!context) {
        LogError("vulkan: failed to allocate context");
        util::reportFatalError("vulkan: failed to allocate context");

        return nullptr;
    }

    context->instance_ = instance;
    context->surface_ = description.surface;
    context->surface_extent_ = description.surface_extent;
    context->framebuffer_extent_ = description.framebuffer_extent;

    const auto physical_devices = getCompatibleDevices(context->instance_, context->surface_);
    if (physical_devices.empty()) {
        LogError("vulkan: there are no supported devices");
        util::reportFatalError("vulkan: there are no supported devices");

        return nullptr;
    }

    const auto &selected_device = physical_devices.front();

    context->physical_device_ = selected_device.first;
    context->graphics_queue_family_ = selected_device.second.graphics;
    context->present_queue_family_ = selected_device.second.present;

    constexpr std::array<float, 1> queue_priorities = {1.0f};
    std::vector<VkDeviceQueueCreateInfo> queue_create_infos = {};

    const auto add_queue_create_info = [&](uint32_t queue_family) {
        queue_create_infos.emplace_back(
            VkDeviceQueueCreateInfo{
                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                .pNext = nullptr,
                .queueFamilyIndex = queue_family,
                .queueCount = 1,
                .pQueuePriorities = queue_priorities.data(),
            });
    };

    add_queue_create_info(context->graphics_queue_family_);
    if (context->present_queue_family_ != context->graphics_queue_family_) {
        add_queue_create_info(context->present_queue_family_);
    }

    std::vector<const char *> device_extensions{kRequiredDeviceExtensions.begin(), kRequiredDeviceExtensions.end()};

    VkPhysicalDeviceVulkan11Features device_features_11 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .pNext = nullptr,
    };

    VkPhysicalDeviceVulkan12Features device_features_12 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &device_features_11,
        .descriptorIndexing = true,
        .descriptorBindingVariableDescriptorCount = true,
        .runtimeDescriptorArray = true,
        .bufferDeviceAddress = true,
    };

    VkPhysicalDeviceVulkan13Features device_features_13 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &device_features_12,
        .synchronization2 = true,
        .dynamicRendering = true,
    };

    VkPhysicalDeviceFeatures device_features_10 = {
        .samplerAnisotropy = VK_TRUE,
    };

    VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &device_features_13,
        .queueCreateInfoCount = static_cast<uint32_t>(queue_create_infos.size()),
        .pQueueCreateInfos = queue_create_infos.data(),
        .enabledExtensionCount = static_cast<uint32_t>(device_extensions.size()),
        .ppEnabledExtensionNames = device_extensions.data(),
        .pEnabledFeatures = &device_features_10,
    };

    VK_CHECK_ERROR(vkCreateDevice(context->physical_device_, &device_info, nullptr, &context->device_));
    vkGetDeviceQueue(context->device_, context->graphics_queue_family_, 0, &context->graphics_queue_);
    vkGetDeviceQueue(context->device_, context->present_queue_family_, 0, &context->present_queue_);

    LogInfo("vulkan: device was initialized successfully");

    if (!Instance::s_initialized_device_loader_) {
        LogInfo("vulkan: volk not initialized yet, load device function pointers");
        volkLoadDevice(context->device_);

        Instance::s_initialized_device_loader_ = true;
    }

    // initialize the allocator
    context->allocator_funcs_ = VmaVulkanFunctions{
        .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr = vkGetDeviceProcAddr,
        .vkCreateBuffer = vkCreateBuffer,
        .vkCreateImage = vkCreateImage,
    };

    VmaAllocatorCreateInfo allocator_info = {
        .physicalDevice = context->physical_device_,
        .device = context->device_,
        .pVulkanFunctions = &context->allocator_funcs_,
        .instance = context->instance_,
        .vulkanApiVersion = VK_API_VERSION_1_3,
    };

    VK_CHECK_ERROR(vmaCreateAllocator(&allocator_info, &context->allocator_));
    LogInfo("vulkan: vma was initialized");

    context->create_swapchain();
    return context;
}

auto Instance::Context::create_swapchain() -> void {
    VkSurfaceCapabilitiesKHR capabilities = {};
    VK_CHECK_ERROR(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &capabilities));

    uint32_t num_formats = 0;
    VK_CHECK_ERROR(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &num_formats, nullptr));

    if (0 == num_formats) {
        LogError("vulkan: cannot create swapchain, no available formats");
        util::reportFatalError("vulkan: cannot create swapchain, no available formats");

        return;
    }

    std::vector<VkSurfaceFormatKHR> formats{num_formats};
    VK_CHECK_ERROR(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &num_formats, formats.data()));

    uint32_t num_present_modes = 0;
    VK_CHECK_ERROR(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_, &num_present_modes, nullptr));

    if (0 == num_formats) {
        LogError("vulkan: cannot create swapchain, no available present modes");
        util::reportFatalError("vulkan: cannot create swapchain, no available present modes");

        return;
    }

    std::vector<VkPresentModeKHR> present_modes{num_present_modes};
    VK_CHECK_ERROR(vkGetPhysicalDeviceSurfacePresentModesKHR(
        physical_device_, surface_, &num_present_modes, present_modes.data()));

    // try to pick srgb format
    const auto selected_format = [&]() -> VkSurfaceFormatKHR {
        const auto srgb_format = std::find_if(formats.begin(), formats.end(), [&](const VkSurfaceFormatKHR &format) {
            return format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });

        if (formats.end() != srgb_format) {
            return *srgb_format;
        }

        LogWarning("vulkan: surface does not support any srgb formats");
        return formats.front();
    }();

    const auto selected_extent = [&]() -> VkExtent2D {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return capabilities.currentExtent;
        } else {
            return VkExtent2D{
                std::clamp(
                    framebuffer_extent_.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
                std::clamp(
                    framebuffer_extent_.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height),
            };
        }
    }();

    auto selected_present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
    if (present_modes.end() == std::find(present_modes.begin(), present_modes.end(), selected_present_mode)) {
        selected_present_mode = VK_PRESENT_MODE_FIFO_KHR;
        LogWarning("vulkan: surface does not support mailbox present mode, fallback to fifo");
    }

    std::array<uint32_t, 2> queue_families{graphics_queue_family_, present_queue_family_};
    const auto image_count = (capabilities.maxImageCount > 0)
                                 ? std::min(capabilities.minImageCount + 1, capabilities.maxImageCount)
                                 : capabilities.minImageCount + 1;

    const auto use_sharing = (graphics_queue_family_ != present_queue_family_);

    if (0 != swapchain_image_views_.size()) {
        LogInfo("destroy old image views for swapchain");
        for (const auto &image_view : swapchain_image_views_) {
            vkDestroyImageView(device_, image_view, nullptr);
        }

        swapchain_image_views_.clear();
    }

    VkSwapchainCreateInfoKHR swapchain_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = nullptr,
        .surface = surface_,
        .minImageCount = image_count,
        .imageFormat = selected_format.format,
        .imageColorSpace = selected_format.colorSpace,
        .imageExtent = selected_extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = use_sharing ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
        .pQueueFamilyIndices = use_sharing ? queue_families.data() : nullptr,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = selected_present_mode,
        .clipped = true,
        .oldSwapchain = swapchain_,
    };

    VK_CHECK_ERROR(vkCreateSwapchainKHR(device_, &swapchain_info, nullptr, &swapchain_));
    LogInfo("vulkan: swapchain created successfully");

    VK_CHECK_ERROR(vkGetSwapchainImagesKHR(device_, swapchain_, &num_swapchain_images_, nullptr));

    swapchain_images_.resize(num_swapchain_images_);
    VK_CHECK_ERROR(vkGetSwapchainImagesKHR(device_, swapchain_, &num_swapchain_images_, swapchain_images_.data()));

    LogInfo("vulkan: create image views for swapchain");
    std::transform(
        swapchain_images_.begin(), swapchain_images_.end(), std::back_inserter(swapchain_image_views_),
        [&](const auto &image) {
        VkImageView image_view = VK_NULL_HANDLE;
        VkImageViewCreateInfo image_view_info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .image = image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = selected_format.format,
            .components =
                {
                    .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .a = VK_COMPONENT_SWIZZLE_IDENTITY,
                },
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };

        VK_CHECK_ERROR(vkCreateImageView(device_, &image_view_info, nullptr, &image_view));
        return image_view;
    });
}

Instance::Context::~Context() noexcept {
    if (0 != swapchain_image_views_.size()) {
        for (const auto &image_view : swapchain_image_views_) {
            vkDestroyImageView(device_, image_view, nullptr);
        }

        swapchain_image_views_.clear();
    }

    if (VK_NULL_HANDLE != swapchain_) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }

    if (VK_NULL_HANDLE != allocator_) {
        vmaDestroyAllocator(allocator_);
        allocator_ = VK_NULL_HANDLE;
    }

    if (VK_NULL_HANDLE != device_) {
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }

    if (VK_NULL_HANDLE != surface_) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }
}

Instance::~Instance() noexcept {
    LogInfo("vulkan: execute cleanup");

    if (VK_NULL_HANDLE != debug_messenger_) {
        vkDestroyDebugUtilsMessengerEXT(instance_, debug_messenger_, nullptr);
        debug_messenger_ = VK_NULL_HANDLE;
    }

    if (VK_NULL_HANDLE != instance_) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }

    LogInfo("vulkan: cleanup complete");
}

} // namespace graphics
