#pragma once
#include <array>
#include <vector>

#include "common/refcounted.hpp"

#include "graphics/common.hpp"
#include "graphics/renderer/resource.hpp"

namespace graphics {

enum class DescriptorDataType {
    eShaderStorageBuffer,
    eUniformBuffer,
    eSamplerTexture,
};

enum class DescriptorShaderStage : VkShaderStageFlags {
    eVertex = VK_SHADER_STAGE_VERTEX_BIT,
    eTessControl = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
    eTessEvaluation = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
    eGeometry = VK_SHADER_STAGE_GEOMETRY_BIT,
    eFragment = VK_SHADER_STAGE_FRAGMENT_BIT,
    eCompute = VK_SHADER_STAGE_COMPUTE_BIT,
    eAllGraphics = VK_SHADER_STAGE_ALL_GRAPHICS,
    eAll = VK_SHADER_STAGE_ALL,
};

auto operator|(const DescriptorShaderStage &s1, const DescriptorShaderStage &s2) -> DescriptorShaderStage;
auto operator&(const DescriptorShaderStage &s1, const DescriptorShaderStage &s2) -> DescriptorShaderStage;
auto operator~(const DescriptorShaderStage &s) -> DescriptorShaderStage;

struct DescriptorDescription {
    DescriptorDataType type;
    DescriptorShaderStage stages;
    uint32_t num_bindings;
};

class DescriptorLayout final : RendererResource {
public:
    struct Description {
        std::vector<DescriptorDescription> layout;
    };

    static auto create(IResourceScheduler *scheduler, const Description &description)
        -> util::RefCountedPtr<DescriptorLayout>;

    ~DescriptorLayout() noexcept;

    DescriptorLayout(const DescriptorLayout &) = delete;
    auto operator=(const DescriptorLayout &) = delete;

    DescriptorLayout(DescriptorLayout &&) = delete;
    auto operator=(DescriptorLayout &&) = delete;

    auto description() const -> const Description & { return description_; }
    auto nativeLayout() const -> VkDescriptorSetLayout { return layout_; }

    auto numSamplerTextures() const -> uint32_t { return num_sampler_textures_; }
    auto numStorageBuffers() const -> uint32_t { return num_storage_buffers_; }
    auto numUniformBuffers() const -> uint32_t { return num_uniform_buffers_; }
    auto variableDescMaxCount() const -> uint32_t { return variable_desc_max_count_; }

private:
    DescriptorLayout(IResourceScheduler *scheduler, VkDescriptorSetLayout layout, const Description &description)
        : RendererResource{scheduler}, layout_{layout}, description_{description} {}

    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
    Description description_;

    uint32_t num_sampler_textures_ = 0;
    uint32_t num_storage_buffers_ = 0;
    uint32_t num_uniform_buffers_ = 0;
    uint32_t variable_desc_max_count_ = 0;
};

template <uint32_t kNumSets> class DescriptorSetArray final : public RendererResource {
public:
    static auto create(IResourceScheduler *scheduler, const DescriptorLayout::Description &description)
        -> util::RefCountedPtr<DescriptorSetArray> {
        util::RefCountedPtr<DescriptorSetArray> array{new (std::nothrow) DescriptorSetArray()};
        if (!array) {
            LogError("vulkan: cannot allocate descriptor set array");
            return nullptr;
        }

        array->scheduler_ = scheduler;
        array->layout_ = DescriptorLayout::create(scheduler, description);

        const auto num_sampler_textures = array->layout_->numSamplerTextures();
        const auto num_storage_buffers = array->layout_->numStorageBuffers();
        const auto num_uniform_buffers = array->layout_->numUniformBuffers();
        const auto variable_desc_max_count = array->layout_->variableDescMaxCount();

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

        VK_CHECK_ERROR(vkCreateDescriptorPool(scheduler->context()->device(), &pool_info, nullptr, &array->pool_));

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
            layouts[i] = array->layout_;
        }

        VkDescriptorSetAllocateInfo set_alloc_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .pNext = (variable_desc_max_count > 0) ? &variable_desc_info : nullptr,
            .descriptorPool = array->pool_,
            .descriptorSetCount = kNumSets,
            .pSetLayouts = layouts.data(),
        };

        VK_CHECK_ERROR(vkAllocateDescriptorSets(scheduler->context()->device(), &set_alloc_info, array->sets_.data()));
        return array;
    }

    ~DescriptorSetArray() noexcept {
        const auto device = scheduler()->context()->device();

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
    auto description() const -> const DescriptorLayout::Description & { return layout_->description(); }
    auto layout() const -> DescriptorLayout * { return layout_; }
    auto getSetForFrame(uint32_t frame) const -> VkDescriptorSet { return sets_[frame]; }

private:
    DescriptorSetArray() = default;

    Context *context_ = nullptr;

    util::RefCountedPtr<DescriptorLayout> layout_ = {};
    VkDescriptorPool pool_ = VK_NULL_HANDLE;

    std::array<VkDescriptorSet, kNumSets> sets_;
};

} // namespace graphics
