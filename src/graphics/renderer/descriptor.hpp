#pragma once
#include <array>
#include <vector>

#include <volk.h>

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

class DescriptorPool final {
public:
    struct Description {
        struct PoolSize {
            DescriptorDataType type;
            uint32_t count;
        };

        std::vector<PoolSize> pool_sizes;
        uint32_t max_sets;
    };

    static auto create(Context *context, const Description &description) -> DescriptorPool;

    DescriptorPool() = default;
    ~DescriptorPool() noexcept;

    DescriptorPool(const DescriptorPool &) = delete;
    auto operator=(const DescriptorPool &) = delete;

    DescriptorPool(DescriptorPool &&) noexcept;
    auto operator=(DescriptorPool &&) noexcept -> DescriptorPool &;

    operator bool() const { return valid(); }
    auto valid() const -> bool { return VK_NULL_HANDLE != pool_; }

    auto nativeDescriptorPool() const -> VkDescriptorPool { return pool_; }
    auto maxSets() const -> uint32_t { return description_.max_sets; }
    auto poolSizes() const -> const std::vector<Description::PoolSize> & { return description_.pool_sizes; }

private:
    DescriptorPool(Context *context, VkDescriptorPool pool, const Description &description)
        : context_{context}, pool_{pool}, description_{description} {}

    Context *context_ = nullptr;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    Description description_ = {};
};

class DescriptorLayout final : public RendererResource {
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
        auto array =
            util::RefCountedPtr<DescriptorSetArray>::create(new (std::nothrow) DescriptorSetArray<kNumSets>(scheduler));
        if (!array) {
            LogError("vulkan: cannot allocate descriptor set array");
            return nullptr;
        }

        array->layout_ = DescriptorLayout::create(scheduler, description);

        const auto num_sampler_textures = array->layout_->numSamplerTextures();
        const auto num_storage_buffers = array->layout_->numStorageBuffers();
        const auto num_uniform_buffers = array->layout_->numUniformBuffers();
        const auto variable_desc_max_count = array->layout_->variableDescMaxCount();

        DescriptorPool::Description pool_description = {
            .max_sets = kNumSets,
        };

        if (num_sampler_textures > 0) {
            pool_description.pool_sizes.emplace_back(
                DescriptorPool::Description::PoolSize{
                    DescriptorDataType::eSamplerTexture, num_sampler_textures * kNumSets});
        }

        if (num_storage_buffers > 0) {
            pool_description.pool_sizes.emplace_back(
                DescriptorPool::Description::PoolSize{
                    DescriptorDataType::eShaderStorageBuffer, num_storage_buffers * kNumSets});
        }

        if (num_uniform_buffers > 0) {
            pool_description.pool_sizes.emplace_back(
                DescriptorPool::Description::PoolSize{
                    DescriptorDataType::eUniformBuffer, num_uniform_buffers * kNumSets});
        }

        array->pool_ = DescriptorPool::create(scheduler->context(), pool_description);

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
            layouts[i] = array->layout()->nativeLayout();
        }

        VkDescriptorSetAllocateInfo set_alloc_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .pNext = (variable_desc_max_count > 0) ? &variable_desc_info : nullptr,
            .descriptorPool = array->pool().nativeDescriptorPool(),
            .descriptorSetCount = kNumSets,
            .pSetLayouts = layouts.data(),
        };

        VK_CHECK_ERROR(vkAllocateDescriptorSets(scheduler->context()->device(), &set_alloc_info, array->sets_.data()));
        return array;
    }

    ~DescriptorSetArray() noexcept = default;

    DescriptorSetArray(const DescriptorSetArray &) = delete;
    auto operator=(const DescriptorSetArray &) = delete;

    DescriptorSetArray(DescriptorSetArray &&) noexcept = delete;
    auto operator=(DescriptorSetArray &&) noexcept = delete;

    auto pool() const -> const DescriptorPool & { return pool_; }
    auto description() const -> const DescriptorLayout::Description & { return layout_->description(); }
    auto layout() const -> DescriptorLayout * { return layout_; }
    auto getSetForFrame(uint32_t frame) const -> VkDescriptorSet { return sets_[frame]; }

private:
    DescriptorSetArray(IResourceScheduler *scheduler) : RendererResource{scheduler} {}

    Context *context_ = nullptr;

    util::RefCountedPtr<DescriptorLayout> layout_ = {};
    DescriptorPool pool_ = {};

    std::array<VkDescriptorSet, kNumSets> sets_;
};

} // namespace graphics
