#include "common.hpp"

namespace graphics {

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

} // namespace graphics
