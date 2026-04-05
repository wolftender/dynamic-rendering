#pragma once
#include <vulkan/vulkan.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vk_mem_alloc.h>

// this include is needed for error reporting
#include "common/utility.hpp" // IWYU pragma: keep

#include "logger.hpp"

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

namespace graphics {

auto messageSeverityToString(VkDebugUtilsMessageSeverityFlagBitsEXT s) -> std::string_view;
auto messageTypeToString(VkDebugUtilsMessageTypeFlagsEXT s) -> std::string_view;
auto physicalDeviceTypeToString(const VkPhysicalDeviceType &type) -> std::string_view;

} // namespace graphics
