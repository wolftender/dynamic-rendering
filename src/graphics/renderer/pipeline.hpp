#pragma once
#include "common/refcounted.hpp"

#include "graphics/vulkan.hpp"
#include "graphics/renderer/descriptor.hpp"
#include "graphics/renderer/format.hpp"
#include "graphics/renderer/resource.hpp"

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace graphics {

class ComputePipeline final : public RendererResource {
public:
    ~ComputePipeline() noexcept;

    ComputePipeline(const ComputePipeline &) = delete;
    auto operator=(const ComputePipeline &) = delete;

    ComputePipeline(ComputePipeline &&) = delete;
    auto operator=(ComputePipeline &&) = delete;

    auto nativeShaderModule() const -> VkShaderModule { return shader_module_; }
    auto nativePipelineLayout() const -> VkPipelineLayout { return layout_; }
    auto nativePipeline() const -> VkPipeline { return pipeline_; }

    auto descriptorSetLayouts() const -> const std::vector<util::RefCountedPtr<DescriptorLayout>> & {
        return descriptor_set_layouts_;
    }

private:
    ComputePipeline(
        IResourceScheduler *scheduler, VkShaderModule shader_module, VkPipelineLayout layout, VkPipeline pipeline)
        : RendererResource{scheduler}, context_{scheduler->context()}, shader_module_{shader_module}, layout_{layout},
          pipeline_{pipeline} {}

    Context *context_ = nullptr;
    VkShaderModule shader_module_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    std::vector<util::RefCountedPtr<DescriptorLayout>> descriptor_set_layouts_;
    friend class ComputePipelineBuilder;
};

class RenderPipeline final : public RendererResource {
public:
    ~RenderPipeline() noexcept;

    RenderPipeline(const RenderPipeline &) = delete;
    auto operator=(const RenderPipeline &) = delete;

    RenderPipeline(RenderPipeline &&) = delete;
    auto operator=(RenderPipeline &&) = delete;

    auto nativeShaderModule() const -> VkShaderModule { return shader_module_; }
    auto nativePipelineLayout() const -> VkPipelineLayout { return layout_; }
    auto nativePipeline() const -> VkPipeline { return pipeline_; }

    auto descriptorSetLayouts() const -> const std::vector<util::RefCountedPtr<DescriptorLayout>> & {
        return descriptor_set_layouts_;
    }

private:
    RenderPipeline(
        IResourceScheduler *scheduler, VkShaderModule shader_module, VkPipelineLayout layout, VkPipeline pipeline)
        : RendererResource{scheduler}, context_{scheduler->context()}, shader_module_{shader_module}, layout_{layout},
          pipeline_{pipeline} {}

    Context *context_ = nullptr;
    VkShaderModule shader_module_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    std::vector<util::RefCountedPtr<DescriptorLayout>> descriptor_set_layouts_;
    friend class RenderPipelineBuilder;
};

struct ShaderDescription {
    VkShaderStageFlags stage;
    std::string entry_point;
};

struct VertexLayoutElement {
    uint32_t location;
    uint32_t binding;
    uint32_t offset;
    Format format;

    operator VkVertexInputAttributeDescription() const {
        return VkVertexInputAttributeDescription{location, binding, formatToVk(format), offset};
    }
};

class ComputePipelineBuilder final {
public:
    ComputePipelineBuilder(IResourceScheduler *scheduler);
    ~ComputePipelineBuilder() noexcept = default;

    ComputePipelineBuilder(const ComputePipelineBuilder &) = delete;
    auto operator=(const ComputePipelineBuilder &) = delete;

    ComputePipelineBuilder(ComputePipelineBuilder &&) = delete;
    auto operator=(ComputePipelineBuilder &&) = delete;

    auto withShaderStage(std::span<const uint32_t> bytecode, std::span<const ShaderDescription> descriptions)
        -> ComputePipelineBuilder &;

    auto withDescriptorSet(util::RefCountedPtr<DescriptorLayout> set_layout) -> ComputePipelineBuilder &;

    auto withPushConstant(VkDeviceSize size, VkShaderStageFlags stages) -> ComputePipelineBuilder &;

    template <typename T> auto withPushConstant(VkShaderStageFlags stages) -> ComputePipelineBuilder & {
        return withPushConstant(sizeof(T), stages);
    }

    auto build() -> util::RefCountedPtr<ComputePipeline>;

private:
    IResourceScheduler *scheduler_ = nullptr;

    std::vector<util::RefCountedPtr<DescriptorLayout>> descriptor_set_layouts_;
    std::vector<VkPushConstantRange> push_constant_ranges_;
    std::vector<uint32_t> shader_bytecode_;
    std::vector<ShaderDescription> shader_descriptions_;
};

class RenderPipelineBuilder final {
public:
    static auto blendAttachmentNoBlending() -> VkPipelineColorBlendAttachmentState;
    static auto blendAttachmentAlphaBlending() -> VkPipelineColorBlendAttachmentState;

    RenderPipelineBuilder(IResourceScheduler *scheduler);
    ~RenderPipelineBuilder() noexcept = default;

    RenderPipelineBuilder(const RenderPipelineBuilder &) = delete;
    auto operator=(const RenderPipelineBuilder &) = delete;

    RenderPipelineBuilder(RenderPipelineBuilder &&) = delete;
    auto operator=(RenderPipelineBuilder &&) = delete;

    auto withShaderStage(std::span<const uint32_t> bytecode, std::span<const ShaderDescription> descriptions)
        -> RenderPipelineBuilder &;

    auto withDescriptorSet(util::RefCountedPtr<DescriptorLayout> set_layout) -> RenderPipelineBuilder &;

    auto withPushConstant(VkDeviceSize size, VkShaderStageFlags stages) -> RenderPipelineBuilder &;

    template <typename T> auto withPushConstant(VkShaderStageFlags stages) -> RenderPipelineBuilder & {
        return withPushConstant(sizeof(T), stages);
    }

    auto withPrimitiveTopology(VkPrimitiveTopology topology) -> RenderPipelineBuilder &;

    template <typename T>
    auto withVertexLayout(
        std::span<const VertexLayoutElement> attributes, uint32_t binding = 0,
        VkVertexInputRate input_rate = VK_VERTEX_INPUT_RATE_VERTEX) -> RenderPipelineBuilder & {
        vertex_input_desc_ = VkVertexInputBindingDescription{
            .binding = binding,
            .stride = sizeof(T),
            .inputRate = input_rate,
        };

        vertex_input_attribs_.clear();
        for (const auto &attribute : attributes) {
            vertex_input_attribs_.push_back(attribute);
        }

        return *this;
    }

    auto withDepthAttachment(Format depth_format) -> RenderPipelineBuilder &;

    auto addColorAttachment(
        Format color_format, VkPipelineColorBlendAttachmentState blend_attachment = blendAttachmentNoBlending())
        -> RenderPipelineBuilder &;

    auto addColorAttachmentAlphaBlend(Format color_format) -> RenderPipelineBuilder &;

    auto withRasterizationDesc(VkPipelineRasterizationStateCreateInfo desc) -> RenderPipelineBuilder &;
    auto withMultisampleDesc(VkPipelineMultisampleStateCreateInfo desc) -> RenderPipelineBuilder &;

    auto withPolygonMode(VkPolygonMode mode) -> RenderPipelineBuilder &;
    auto withCullMode(VkCullModeFlags mode) -> RenderPipelineBuilder &;
    auto withFrontFace(VkFrontFace face) -> RenderPipelineBuilder &;
    auto withSampleCount(VkSampleCountFlagBits count) -> RenderPipelineBuilder &;

    template <std::invocable<VkPipelineRasterizationStateCreateInfo &> F>
    auto useRasterizationDesc(F builder) -> RenderPipelineBuilder & {
        builder(rasterization_desc_);
        return *this;
    }

    template <std::invocable<VkPipelineMultisampleStateCreateInfo &> F>
    auto useMultisampleDesc(F builder) -> RenderPipelineBuilder & {
        builder(multisample_desc_);
        return *this;
    }

    auto build() -> util::RefCountedPtr<RenderPipeline>;

private:
    IResourceScheduler *scheduler_ = nullptr;

    std::vector<util::RefCountedPtr<DescriptorLayout>> descriptor_set_layouts_;

    std::vector<VkVertexInputAttributeDescription> vertex_input_attribs_;
    std::vector<uint32_t> shader_bytecode_;
    std::vector<ShaderDescription> shader_descriptions_;

    std::vector<Format> color_attachment_formats_;
    std::vector<VkPipelineColorBlendAttachmentState> color_blend_attachments_;
    std::optional<Format> depth_attachment_format_;

    std::vector<VkPushConstantRange> push_constant_ranges_;

    std::optional<VkVertexInputBindingDescription> vertex_input_desc_ = {};
    VkPipelineInputAssemblyStateCreateInfo input_assembly_desc_ = {};
    VkPipelineDepthStencilStateCreateInfo depth_stencil_desc_ = {};
    VkPipelineRasterizationStateCreateInfo rasterization_desc_ = {};
    VkPipelineMultisampleStateCreateInfo multisample_desc_ = {};
};

} // namespace graphics