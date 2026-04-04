#pragma once
#include <array>
#include <vector>

#include "graphics/common.hpp"
#include "graphics/renderer/resource.hpp"
#include "graphics/renderer/scheduler.hpp"

namespace graphics {

template <uint32_t kNumSets> class DescriptorSetArray final : public RendererResource {
public:
    enum class DescriptorDataType {
        eShaderStorageBuffer,
        eUniformBuffer,
        eSamplerTexture,
    };

    struct DescriptorDescription {
        DescriptorDataType type;
        VkShaderStageFlags stages;
        uint32_t num_bindings;
    };

    struct Description {
        std::vector<DescriptorDescription> layout;
    };

    static auto create(RendererScheduler *scheduler, const Description &description)
        -> util::RefCountedPtr<DescriptorSetArray> {
        util::RefCountedPtr<DescriptorSetArray> helper{new (std::nothrow) DescriptorSetArray()};
        if (!helper) {
            LogError("vulkan: cannot allocate descriptor set array");
            return nullptr;
        }

        helper->scheduler_ = scheduler;
        helper->desc_ = std::move(description);

        uint32_t num_layout_elements = helper->desc_.layout.size();

        std::vector<VkDescriptorSetLayoutBinding> bindings;
        std::vector<VkDescriptorBindingFlags> binding_flags;

        bindings.resize(num_layout_elements);
        binding_flags.resize(num_layout_elements);

        uint32_t num_sampler_textures = 0;
        uint32_t num_storage_buffers = 0;
        uint32_t num_uniform_buffers = 0;
        uint32_t variable_desc_max_count = 0;

        for (size_t i = 0; i < num_layout_elements; ++i) {
            const auto &element = helper->desc_.layout[i];
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

        const auto device = helper->renderer_->context_->device();
        VK_CHECK_ERROR(vkCreateDescriptorSetLayout(device, &layout_desc, nullptr, &helper->layout_));

        std::vector<VkDescriptorPoolSize> pool_sizes;
        if (num_sampler_textures > 0) {
            pool_sizes.push_back(
                VkDescriptorPoolSize{
                    .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .descriptorCount = num_sampler_textures * kNumSets,
                });
        }

        if (num_storage_buffers > 0) {
            pool_sizes.push_back(
                VkDescriptorPoolSize{
                    .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    .descriptorCount = num_storage_buffers * kNumSets,
                });
        }

        if (num_uniform_buffers > 0) {
            pool_sizes.push_back(
                VkDescriptorPoolSize{
                    .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .descriptorCount = num_uniform_buffers * kNumSets,
                });
        }

        VkDescriptorPoolCreateInfo pool_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = kNumSets,
            .poolSizeCount = static_cast<uint32_t>(pool_sizes.size()),
            .pPoolSizes = pool_sizes.data(),
        };

        VK_CHECK_ERROR(vkCreateDescriptorPool(device, &pool_info, nullptr, &helper->pool_));

        // allocate the descriptor sets
        std::array<uint32_t, kNumSets> desc_counts;
        for (size_t i = 0; i < kNumSets; ++i) {
            desc_counts[i] = variable_desc_max_count;
        }

        VkDescriptorSetVariableDescriptorCountAllocateInfo variable_desc_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
            .descriptorSetCount = kNumSets,
            .pDescriptorCounts = desc_counts.data(),
        };

        // https://github.com/KhronosGroup/Vulkan-Docs/issues/1236
        std::array<VkDescriptorSetLayout, kNumSets> layouts;
        for (size_t i = 0; i < kNumSets; ++i) {
            layouts[i] = helper->layout_;
        }

        VkDescriptorSetAllocateInfo set_alloc_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .pNext = (variable_desc_max_count > 0) ? &variable_desc_info : nullptr,
            .descriptorPool = helper->pool_,
            .descriptorSetCount = kNumSets,
            .pSetLayouts = layouts.data(),
        };

        VK_CHECK_ERROR(vkAllocateDescriptorSets(device, &set_alloc_info, helper->sets_.data()));
        return helper;
    }

    ~DescriptorSetArray() noexcept {
        const auto device = scheduler_->context()->device();

        if (VK_NULL_HANDLE != layout_) {
            vkDestroyDescriptorSetLayout(device, layout_, nullptr);
            layout_ = VK_NULL_HANDLE;
        }

        if (VK_NULL_HANDLE != pool_) {
            vkDestroyDescriptorPool(device, pool_, nullptr);
            pool_ = VK_NULL_HANDLE;
        }
    }

    DescriptorSetArray(const DescriptorSetArray &) = delete;
    auto operator=(const DescriptorSetArray &) = delete;

    DescriptorSetArray(DescriptorSetArray &&) noexcept = delete;
    auto operator=(DescriptorSetArray &&) noexcept = delete;

    auto pool() const -> VkDescriptorPool { return pool_; }
    auto description() const -> const Description & { return desc_; }
    auto layout() const -> VkDescriptorSetLayout { return layout_; }
    auto getSetForFrame(uint32_t frame) const -> VkDescriptorSet { return sets_[frame]; }

private:
    DescriptorSetArray() = default;

    RendererScheduler *scheduler_ = nullptr;
    Description desc_;

    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;

    std::array<VkDescriptorSet, kNumSets> sets_;
};

} // namespace graphics
