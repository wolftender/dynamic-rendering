#include <volk.h>

#include "graphics/renderer/descriptor.hpp"

namespace graphics {

auto operator|(const DescriptorShaderStage &s1, const DescriptorShaderStage &s2) -> DescriptorShaderStage {
    return static_cast<DescriptorShaderStage>(
        static_cast<VkShaderStageFlags>(s1) | static_cast<VkShaderStageFlags>(s2));
}

auto operator&(const DescriptorShaderStage &s1, const DescriptorShaderStage &s2) -> DescriptorShaderStage {
    return static_cast<DescriptorShaderStage>(
        static_cast<VkShaderStageFlags>(s1) & static_cast<VkShaderStageFlags>(s2));
}

auto operator~(const DescriptorShaderStage &s) -> DescriptorShaderStage {
    return static_cast<DescriptorShaderStage>(~static_cast<VkShaderStageFlags>(s));
}

auto DescriptorLayout::create(IResourceScheduler *scheduler, const Description &description)
    -> util::RefCountedPtr<DescriptorLayout> {

    uint32_t num_layout_elements = description.layout.size();

    std::vector<VkDescriptorSetLayoutBinding> bindings;
    std::vector<VkDescriptorBindingFlags> binding_flags;

    bindings.resize(num_layout_elements);
    binding_flags.resize(num_layout_elements);

    uint32_t num_sampler_textures = 0;
    uint32_t num_storage_buffers = 0;
    uint32_t num_uniform_buffers = 0;
    uint32_t variable_desc_max_count = 0;

    for (size_t i = 0; i < num_layout_elements; ++i) {
        const auto &element = description.layout[i];
        bindings[i].binding = i;
        bindings[i].descriptorCount = element.num_bindings;
        bindings[i].stageFlags = static_cast<VkShaderStageFlags>(element.stages);

        switch (element.type) {
        case DescriptorDataType::eSamplerTexture:
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            num_sampler_textures += element.num_bindings;

            break;
        case DescriptorDataType::eShaderStorageBuffer:
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            num_storage_buffers += element.num_bindings;

            break;
        case DescriptorDataType::eUniformBuffer:
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            num_uniform_buffers += element.num_bindings;

            break;
        default:
            LogError("vulkan: invalid descriptor data type");
            return nullptr;
        }

        if (element.num_bindings == 1) {
            binding_flags[i] = 0;
        } else if (element.num_bindings > 1) {
            binding_flags[i] = VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
            if (variable_desc_max_count > 0) {
                LogError("vulkan: invalid descriptor set layout, cant have more than one VLA");
                return nullptr;
            }

            variable_desc_max_count = element.num_bindings;
        } else {
            LogError("vulkan: invalid binding count");
            return nullptr;
        }
    }

    VkDescriptorSetLayoutBindingFlagsCreateInfo layout_flags_desc = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount = static_cast<uint32_t>(binding_flags.size()),
        .pBindingFlags = binding_flags.data(),
    };

    VkDescriptorSetLayoutCreateInfo layout_desc = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = &layout_flags_desc,
        .bindingCount = static_cast<uint32_t>(bindings.size()),
        .pBindings = bindings.data(),
    };

    const auto device = scheduler->context()->device();
    VkDescriptorSetLayout native_layout = VK_NULL_HANDLE;
    VK_CHECK_ERROR(vkCreateDescriptorSetLayout(device, &layout_desc, nullptr, &native_layout));

    util::RefCountedPtr<DescriptorLayout> layout{new DescriptorLayout(scheduler, native_layout, description)};

    layout->num_sampler_textures_ = num_sampler_textures;
    layout->num_storage_buffers_ = num_storage_buffers;
    layout->num_uniform_buffers_ = num_uniform_buffers;
    layout->variable_desc_max_count_ = variable_desc_max_count;

    return layout;
}

DescriptorLayout::~DescriptorLayout() noexcept {
    const auto device = scheduler()->context()->device();

    if (VK_NULL_HANDLE != layout_) {
        vkDestroyDescriptorSetLayout(device, layout_, nullptr);
        layout_ = VK_NULL_HANDLE;
    }
}

} // namespace graphics
