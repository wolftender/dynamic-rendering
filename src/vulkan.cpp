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

Buffer::~Buffer() noexcept { destroy(); }

Buffer::Buffer(Buffer &&b) noexcept {
    destroy();
    device_ = b.device_;
    allocator_ = b.allocator_;

    buffer_ = b.buffer_;
    allocation_ = b.allocation_;
    allocation_info_ = std::move(b.allocation_info_);

    b.device_ = VK_NULL_HANDLE;
    b.allocator_ = VK_NULL_HANDLE;
    b.buffer_ = VK_NULL_HANDLE;
    b.allocation_ = VK_NULL_HANDLE;
}

auto Buffer::operator=(Buffer &&b) noexcept -> Buffer & {
    if (this != &b) {
        destroy();
        device_ = b.device_;
        allocator_ = b.allocator_;

        buffer_ = b.buffer_;
        allocation_ = b.allocation_;
        allocation_info_ = std::move(b.allocation_info_);

        b.device_ = VK_NULL_HANDLE;
        b.allocator_ = VK_NULL_HANDLE;
        b.buffer_ = VK_NULL_HANDLE;
        b.allocation_ = VK_NULL_HANDLE;
    }

    return *this;
}

auto Buffer::memPropFlags() const -> VkMemoryPropertyFlags {
    VkMemoryPropertyFlags props;
    vmaGetAllocationMemoryProperties(allocator_, allocation_, &props);

    return props;
}

auto Buffer::flush(VkDeviceSize offset, VkDeviceSize size) const {
    VK_CHECK_ERROR(vmaFlushAllocation(allocator_, allocation_, offset, size));
}

auto Buffer::deviceAddress() const -> VkDeviceAddress {
    VkBufferDeviceAddressInfo addr_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = buffer_,
    };

    return vkGetBufferDeviceAddress(device_, &addr_info);
}

auto Buffer::destroy() noexcept -> void {
    if (VK_NULL_HANDLE != buffer_) {
        vmaDestroyBuffer(allocator_, buffer_, allocation_);
    }

    buffer_ = VK_NULL_HANDLE;
    allocation_ = VK_NULL_HANDLE;
    allocator_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;

    allocation_info_ = {};
}

Image::View::~View() noexcept { destroy(); }

Image::View::View(View &&v) noexcept {
    destroy();
    image_view_ = v.image_view_;
    image_ = v.image_;
    device_ = v.device_;

    v.image_view_ = VK_NULL_HANDLE;
    v.image_ = nullptr;
    v.device_ = VK_NULL_HANDLE;
}

auto Image::View::operator=(View &&v) noexcept -> View & {
    if (this != &v) {
        destroy();

        image_view_ = v.image_view_;
        image_ = v.image_;
        device_ = v.device_;

        v.image_view_ = VK_NULL_HANDLE;
        v.image_ = nullptr;
        v.device_ = VK_NULL_HANDLE;
    }

    return *this;
}

auto Image::View::destroy() noexcept -> void {
    if (VK_NULL_HANDLE != image_view_) {
        vkDestroyImageView(device_, image_view_, nullptr);

        image_view_ = VK_NULL_HANDLE;
        image_ = nullptr;
        device_ = VK_NULL_HANDLE;
    }
}

Image::~Image() noexcept { destroy(); }

Image::Image(Image &&i) noexcept {
    destroy();

    device_ = i.device_;
    allocator_ = i.allocator_;
    image_ = i.image_;
    allocation_ = i.allocation_;
    allocation_info_ = std::move(i.allocation_info_);
    format_ = i.format_;

    i.device_ = VK_NULL_HANDLE;
    i.allocator_ = VK_NULL_HANDLE;
    i.image_ = VK_NULL_HANDLE;
    i.allocation_ = VK_NULL_HANDLE;
}

auto Image::operator=(Image &&i) noexcept -> Image & {
    if (this != &i) {
        destroy();

        device_ = i.device_;
        allocator_ = i.allocator_;
        image_ = i.image_;
        allocation_ = i.allocation_;
        allocation_info_ = std::move(i.allocation_info_);
        format_ = i.format_;

        i.device_ = VK_NULL_HANDLE;
        i.allocator_ = VK_NULL_HANDLE;
        i.image_ = VK_NULL_HANDLE;
        i.allocation_ = VK_NULL_HANDLE;
    }

    return *this;
}

auto Image::createView(VkImageViewType type, VkFormat format, VkImageAspectFlags aspect_flags) const -> View {
    VkImageViewCreateInfo view_desc = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = nullptr,
        .image = image_,
        .viewType = type,
        .format = format,
        .subresourceRange =
            {
                .aspectMask = aspect_flags,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };

    VkImageView view = VK_NULL_HANDLE;
    VK_CHECK_ERROR(vkCreateImageView(device_, &view_desc, nullptr, &view));

    View result;
    result.image_view_ = view;
    result.image_ = this;
    result.device_ = device_;

    return result;
}

auto Image::memPropFlags() const -> VkMemoryPropertyFlags {
    VkMemoryPropertyFlags props;
    vmaGetAllocationMemoryProperties(allocator_, allocation_, &props);

    return props;
}

auto Image::flush(VkDeviceSize offset, VkDeviceSize size) const {
    VK_CHECK_ERROR(vmaFlushAllocation(allocator_, allocation_, offset, size));
}

auto Image::destroy() noexcept -> void {
    if (VK_NULL_HANDLE != image_) {
        vmaDestroyImage(allocator_, image_, allocation_);
    }

    allocator_ = VK_NULL_HANDLE;
    allocation_ = VK_NULL_HANDLE;
    image_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;

    allocation_info_ = {};
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

    std::unique_ptr<Instance> instance{new (std::nothrow) Instance()};
    if (!instance) {
        LogError("cannot allocate new context object");
        util::reportFatalError("cannot allocate new context object");

        return nullptr;
    }

    // check if we should enable validation layers
    instance->enable_validation_layers_ = checkIfEnableValidationLayers();

    // instance extensions + layers, some are conditional
    std::vector<const char *> instance_extensions = description.instance_extensions;
    if (instance->enable_validation_layers_) {
        instance_extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    std::vector<const char *> instance_layers = {};
    if (instance->enable_validation_layers_) {
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
        .pNext = instance->enable_validation_layers_ ? &debug_msg_info : nullptr,
        .flags = 0,
        .pApplicationInfo = &application_info,
        .enabledLayerCount = static_cast<uint32_t>(instance_layers.size()),
        .ppEnabledLayerNames = instance_layers.size() > 0 ? instance_layers.data() : nullptr,
        .enabledExtensionCount = static_cast<uint32_t>(instance_extensions.size()),
        .ppEnabledExtensionNames = instance_extensions.size() > 0 ? instance_extensions.data() : nullptr,
    };

    LogInfo("vulkan: create instance");
    VK_CHECK_ERROR(vkCreateInstance(&instance_info, nullptr, &instance->instance_));
    if (!s_initialized_instance_loader_) {
        LogInfo("vulkan: volk not initialized yet, load instance function pointers");

        volkLoadInstance(instance->instance_);
        s_initialized_instance_loader_ = true;
    }

    if (instance->enable_validation_layers_) {
        LogInfo("vulkan: enabled validation layers, create debug messenger");
        VK_CHECK_ERROR(
            vkCreateDebugUtilsMessengerEXT(instance->instance_, &debug_msg_info, nullptr, &instance->debug_messenger_));
    }

    return instance;
}

auto Instance::createContext(const Context::Description &description) -> std::unique_ptr<Context> {
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
        if (!device_features_12.timelineSemaphore || !device_features_12.bufferDeviceAddress ||
            !device_features_12.descriptorIndexing || !device_features_12.descriptorBindingVariableDescriptorCount ||
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
            VkBool32 is_compute = (queue_family_properties[index].queueFlags & VK_QUEUE_COMPUTE_BIT);
            VkBool32 is_graphics_compute = is_graphics && is_compute;
            VkBool32 is_present = VK_FALSE;

            VK_CHECK_ERROR(vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, index, surface, &is_present));

            if (is_graphics_compute && is_present) {
                graphics_family = index;
                present_family = index;
                break;
            } else if (is_graphics_compute && !graphics_family.has_value()) {
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

auto Context::create(VkInstance instance, const Description &description) -> std::unique_ptr<Context> {
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

    // cache physical device properties
    vkGetPhysicalDeviceProperties(context->physical_device_, &context->phys_dev_props_);

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
        .timelineSemaphore = true,
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
        .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
        .physicalDevice = context->physical_device_,
        .device = context->device_,
        .pVulkanFunctions = &context->allocator_funcs_,
        .instance = context->instance_,
        .vulkanApiVersion = VK_API_VERSION_1_3,
    };

    VK_CHECK_ERROR(vmaCreateAllocator(&allocator_info, &context->allocator_));
    LogInfo("vulkan: vma was initialized");

    context->memory_ = MemoryHelper::create(context.get());
    if (!context->memory_) {
        LogError("vulkan: failed to create memory helper");
        return nullptr;
    }

    // initialize swapchain
    context->createSwapchain();

    // get best depth format
    constexpr std::array<VkFormat, 2> kDepthFormats = {VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT};
    for (const auto &format : kDepthFormats) {
        VkFormatProperties2 format_properties = {
            .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
        };

        vkGetPhysicalDeviceFormatProperties2(context->physical_device_, format, &format_properties);
        if (format_properties.formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            context->supported_depth_format_ = format;
        }
    }

    return context;
}

auto Context::MemoryHelper::create(Context *context) -> std::unique_ptr<MemoryHelper> {
    std::unique_ptr<MemoryHelper> memory{new (std::nothrow) MemoryHelper()};
    if (!memory) {
        LogError("vulkan: failed to allocate memory helper");
        util::reportFatalError("vulkan: failed to allocate memory helper");

        return nullptr;
    }

    memory->context_ = context;

    // initialize timeline semaphore for upload timeline
    VkSemaphoreTypeCreateInfo timeline_create_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
        .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
    };

    VkSemaphoreCreateInfo semaphore_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = &timeline_create_info,
    };

    VK_CHECK_ERROR(vkCreateSemaphore(memory->context_->device(), &semaphore_info, nullptr, &memory->upload_semaphore_));

    // command pool for memory helper
    VkCommandPoolCreateInfo command_pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = memory->context_->graphicsQueueFamily(),
    };

    VK_CHECK_ERROR(
        vkCreateCommandPool(memory->context_->device(), &command_pool_info, nullptr, &memory->command_pool_));

    // command buffer for memory management helper
    VkCommandBufferAllocateInfo command_buffer_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = memory->command_pool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    VK_CHECK_ERROR(
        vkAllocateCommandBuffers(memory->context_->device(), &command_buffer_info, &memory->command_buffer_));

    LogInfo("vulkan: memory helper initialized");
    return memory;
}

Context::MemoryHelper::~MemoryHelper() noexcept {
    LogInfo("vulkan: memory helper destroyed");

    vkDestroySemaphore(context_->device(), upload_semaphore_, nullptr);
    vkDestroyCommandPool(context_->device(), command_pool_, nullptr);
}

auto Context::MemoryHelper::beginCommandBuffer() const -> void {
    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    VK_CHECK_ERROR(vkBeginCommandBuffer(command_buffer_, &begin_info));

    LogInfo("vulkan: memory helper begin recording command buffer");
}

auto Context::MemoryHelper::submitCommandBuffer(VkSemaphore semaphore, uint64_t signal_value) const -> void {
    VK_CHECK_ERROR(vkEndCommandBuffer(command_buffer_));

    VkTimelineSemaphoreSubmitInfo timeline_info = {
        .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
        .signalSemaphoreValueCount = 1,
        .pSignalSemaphoreValues = &signal_value,
    };

    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = &timeline_info,
        .commandBufferCount = 1,
        .pCommandBuffers = &command_buffer_,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &semaphore,
    };

    VK_CHECK_ERROR(vkQueueSubmit(context_->graphicsQueue(), 1, &submit_info, VK_NULL_HANDLE));
    LogInfo("vulkan: memory helper submit command buffer");

    VkSemaphoreWaitInfo wait_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .semaphoreCount = 1,
        .pSemaphores = &upload_semaphore_,
        .pValues = &upload_timeline_,
    };

    VK_CHECK_ERROR(vkWaitSemaphores(context_->device(), &wait_info, UINT64_MAX));
    VK_CHECK_ERROR(vkResetCommandPool(context_->device(), command_pool_, 0));

    LogInfo("vulkan: memory helper end command submission logic");
}

auto Context::MemoryHelper::createBuffer(
    VkBufferUsageFlags usage, std::span<const uint8_t> data, VkDeviceSize size) const -> Buffer {
    if (size == 0) {
        size = data.size();
    }

    assert(size >= data.size() && "invalid buffer size requested");

    VkBufferCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateInfo alloc_create_info = {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    };

    VmaAllocation allocation = VK_NULL_HANDLE;
    VkBuffer vk_buffer = VK_NULL_HANDLE;
    VmaAllocationInfo allocation_info = {};

    VK_CHECK_ERROR(vmaCreateBuffer(
        context_->allocator_, &create_info, &alloc_create_info, &vk_buffer, &allocation, &allocation_info));

    Buffer buffer;
    buffer.device_ = context_->device_;
    buffer.allocator_ = context_->allocator_;
    buffer.buffer_ = vk_buffer;
    buffer.allocation_ = allocation;
    buffer.allocation_info_ = std::move(allocation_info);

    auto mem_prop_flags = buffer.memPropFlags();
    if (mem_prop_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        void *mapped_memory;
        VK_CHECK_ERROR(vmaMapMemory(context_->allocator_, buffer.allocation_, &mapped_memory));

        ::memcpy(mapped_memory, data.data(), data.size());

        vmaUnmapMemory(context_->allocator_, buffer.allocation_);
        VK_CHECK_ERROR(vmaFlushAllocation(context_->allocator_, buffer.allocation_, 0, VK_WHOLE_SIZE));
    } else {
        // use staging buffer for upload as the memory is not host-mappable
        auto staging_buffer = createStagingBuffer(data.size());
        void *mapped_memory = staging_buffer.cpuMappedPointer();

        ::memcpy(mapped_memory, data.data(), data.size());

        VK_CHECK_ERROR(vmaFlushAllocation(context_->allocator_, buffer.allocation_, 0, VK_WHOLE_SIZE));

        runOnTransferQueue([&](VkCommandBuffer command_buffer) {
            VkBufferCopy copy_info = {
                .srcOffset = 0,
                .dstOffset = 0,
                .size = data.size(),
            };

            vkCmdCopyBuffer(command_buffer, staging_buffer.buffer(), buffer.buffer(), 1, &copy_info);
        });
    }

    return buffer;
}

auto Context::MemoryHelper::createStagingBuffer(VkDeviceSize size) const -> Buffer {
    VkBufferCreateInfo staging_buffer_desc = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateInfo staging_alloc_info = {
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };

    VmaAllocation allocation = VK_NULL_HANDLE;
    VkBuffer vk_buffer = VK_NULL_HANDLE;
    VmaAllocationInfo allocation_info = {};

    VK_CHECK_ERROR(vmaCreateBuffer(
        context_->allocator_, &staging_buffer_desc, &staging_alloc_info, &vk_buffer, &allocation, &allocation_info));

    Buffer buffer;
    buffer.device_ = context_->device_;
    buffer.allocator_ = context_->allocator_;
    buffer.buffer_ = vk_buffer;
    buffer.allocation_ = allocation;
    buffer.allocation_info_ = std::move(allocation_info);

    return buffer;
}

auto Context::MemoryHelper::createDeviceBuffer(VkBufferUsageFlags usage, VkDeviceSize size) const -> Buffer {
    VkBufferCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateInfo alloc_create_info = {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    };

    VmaAllocation allocation = VK_NULL_HANDLE;
    VkBuffer vk_buffer = VK_NULL_HANDLE;
    VmaAllocationInfo allocation_info = {};

    VK_CHECK_ERROR(vmaCreateBuffer(
        context_->allocator_, &create_info, &alloc_create_info, &vk_buffer, &allocation, &allocation_info));

    Buffer buffer;
    buffer.device_ = context_->device_;
    buffer.allocator_ = context_->allocator_;
    buffer.buffer_ = vk_buffer;
    buffer.allocation_ = allocation;
    buffer.allocation_info_ = std::move(allocation_info);

    return buffer;
}

auto Context::MemoryHelper::createSharedBuffer(VkBufferUsageFlags usage, VkDeviceSize size) const -> Buffer {
    VkBufferCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateInfo alloc_create_info = {
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };

    VmaAllocation allocation = VK_NULL_HANDLE;
    VkBuffer vk_buffer = VK_NULL_HANDLE;
    VmaAllocationInfo allocation_info = {};

    VK_CHECK_ERROR(vmaCreateBuffer(
        context_->allocator_, &create_info, &alloc_create_info, &vk_buffer, &allocation, &allocation_info));

    Buffer buffer;
    buffer.device_ = context_->device_;
    buffer.allocator_ = context_->allocator_;
    buffer.buffer_ = vk_buffer;
    buffer.allocation_ = allocation;
    buffer.allocation_info_ = std::move(allocation_info);

    return buffer;
}

auto Context::MemoryHelper::createImage(const VkImageCreateInfo &image_info) const -> Image {
    VmaAllocationCreateInfo alloc_create_info = {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    };

    VkImage vk_image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo allocation_info = {};

    VK_CHECK_ERROR(vmaCreateImage(
        context_->allocator_, &image_info, &alloc_create_info, &vk_image, &allocation, &allocation_info));

    Image image;
    image.device_ = context_->device_;
    image.allocator_ = context_->allocator_;
    image.image_ = vk_image;
    image.allocation_ = allocation;
    image.allocation_info_ = std::move(allocation_info);
    image.format_ = image_info.format;

    return image;
}

auto Context::MemoryHelper::createImage(
    VkFormat format, VkImageUsageFlags usage, VkImageType type, const VkExtent3D &extent) const -> Image {
    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = type,
        .format = format,
        .extent = extent,
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
    };

    return createImage(image_info);
}

auto Context::MemoryHelper::createImageRgba(
    VkImageUsageFlags usage, VkExtent2D extent, std::span<const uint8_t> data) const -> Image {
    VkDeviceSize image_size = extent.width * extent.height * 4;
    assert(image_size <= data.size() && "invalid data span supplied");

    auto staging_buffer = createStagingBuffer(image_size);
    void *mapped_memory = staging_buffer.cpuMappedPointer();

    ::memcpy(mapped_memory, data.data(), image_size);

    VkImageCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_SRGB,
        .extent = VkExtent3D{extent.width, extent.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    };

    VmaAllocationCreateInfo alloc_create_info = {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    };

    VkImage vk_image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo allocation_info = {};

    VK_CHECK_ERROR(vmaCreateImage(
        context_->allocator_, &create_info, &alloc_create_info, &vk_image, &allocation, &allocation_info));

    Image image;
    image.device_ = context_->device_;
    image.allocator_ = context_->allocator_;
    image.image_ = vk_image;
    image.allocation_ = allocation;
    image.allocation_info_ = std::move(allocation_info);
    image.format_ = VK_FORMAT_R8G8B8A8_SRGB;

    runOnTransferQueue([&](VkCommandBuffer command_buffer) {
        VkImageSubresourceRange range = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };

        VkImageMemoryBarrier2 barrier_unknown_to_transfer_dst{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = 0,
            .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .image = image.image(),
            .subresourceRange = range,
        };

        VkDependencyInfo dep_unknown_to_transfer_dst = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier_unknown_to_transfer_dst,
        };

        vkCmdPipelineBarrier2(command_buffer, &dep_unknown_to_transfer_dst);

        VkBufferImageCopy image_copy = {
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource =
                VkImageSubresourceLayers{
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            .imageExtent = VkExtent3D{extent.width, extent.height, 1},
        };

        vkCmdCopyBufferToImage(
            command_buffer, staging_buffer.buffer(), image.image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
            &image_copy);

        VkImageMemoryBarrier2 barrier_transfer_dst_to_shader_read = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .image = image.image(),
            .subresourceRange = range,
        };

        VkDependencyInfo dep_transfer_dst_to_shader_read = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier_transfer_dst_to_shader_read,
        };

        vkCmdPipelineBarrier2(command_buffer, &dep_transfer_dst_to_shader_read);
    });

    return image;
}

auto Context::resize(VkExtent2D surface_extent, VkExtent2D framebuffer_extent) -> void {
    if (surface_extent.width == 0 || surface_extent.height == 0 || framebuffer_extent.width == 0 ||
        framebuffer_extent.height == 0) {
        return;
    }

    LogInfo(
        "vulkan: resize context, surface_extent = {}x{}, fb_extent = {}x{}", surface_extent.width,
        surface_extent.height, framebuffer_extent.width, framebuffer_extent.height);

    surface_extent_ = surface_extent;
    framebuffer_extent_ = framebuffer_extent;

    rebuildSwapchain();
}

auto Context::createSwapchain() -> void {
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

    swapchain_format_ = selected_format;
    present_mode_ = selected_present_mode;

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

auto Context::rebuildSwapchain() -> void {
    VK_CHECK_ERROR(vkDeviceWaitIdle(device_));

    if (VK_NULL_HANDLE != swapchain_) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }

    createSwapchain();
}

Context::~Context() noexcept {
    // destroy the memory helper explicitly BEFORE the device
    memory_.reset(nullptr);

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
