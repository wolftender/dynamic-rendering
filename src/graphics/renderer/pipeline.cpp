#include <volk.h>

#include "graphics/common.hpp"
#include "graphics/renderer/pipeline.hpp"

namespace graphics {

RenderPipeline::~RenderPipeline() noexcept {
    if (VK_NULL_HANDLE != pipeline_) {
        vkDestroyPipeline(scheduler()->context()->device(), pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }

    if (VK_NULL_HANDLE != layout_) {
        vkDestroyPipelineLayout(scheduler()->context()->device(), layout_, nullptr);
        layout_ = VK_NULL_HANDLE;
    }

    if (VK_NULL_HANDLE != shader_module_) {
        vkDestroyShaderModule(scheduler()->context()->device(), shader_module_, nullptr);
        shader_module_ = VK_NULL_HANDLE;
    }
}

ComputePipeline::~ComputePipeline() noexcept {
    if (VK_NULL_HANDLE != pipeline_) {
        vkDestroyPipeline(scheduler()->context()->device(), pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }

    if (VK_NULL_HANDLE != layout_) {
        vkDestroyPipelineLayout(scheduler()->context()->device(), layout_, nullptr);
        layout_ = VK_NULL_HANDLE;
    }

    if (VK_NULL_HANDLE != shader_module_) {
        vkDestroyShaderModule(scheduler()->context()->device(), shader_module_, nullptr);
        shader_module_ = VK_NULL_HANDLE;
    }
}

ComputePipelineBuilder::ComputePipelineBuilder(IResourceScheduler *scheduler) : scheduler_{scheduler} {}

auto ComputePipelineBuilder::withShaderStage(
    std::span<const uint32_t> bytecode, std::span<const ShaderDescription> descriptions) -> ComputePipelineBuilder & {
    shader_bytecode_.assign(bytecode.begin(), bytecode.end());
    shader_descriptions_.assign(descriptions.begin(), descriptions.end());
    return *this;
}

auto ComputePipelineBuilder::withDescriptorSet(util::RefCountedPtr<DescriptorLayout> set_layout)
    -> ComputePipelineBuilder & {
    descriptor_set_layouts_.push_back(std::move(set_layout));
    return *this;
}

auto ComputePipelineBuilder::withPushConstant(VkDeviceSize size, VkShaderStageFlags stages)
    -> ComputePipelineBuilder & {
    push_constant_ranges_.push_back(
        VkPushConstantRange{
            .stageFlags = stages,
            .offset = 0,
            .size = static_cast<uint32_t>(size),
        });
    return *this;
}

auto ComputePipelineBuilder::build() -> util::RefCountedPtr<ComputePipeline> {
    VkDevice device = scheduler_->context()->device();

    VkShaderModuleCreateInfo shader_module_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = shader_bytecode_.size(),
        .pCode = reinterpret_cast<const uint32_t *>(shader_bytecode_.data()),
    };

    VkShaderModule native_shader_module = VK_NULL_HANDLE;
    VK_CHECK_ERROR(vkCreateShaderModule(device, &shader_module_info, nullptr, &native_shader_module));

    std::vector<VkDescriptorSetLayout> set_layouts;
    set_layouts.reserve(descriptor_set_layouts_.size());

    for (const auto &layout : descriptor_set_layouts_) {
        set_layouts.push_back(layout->nativeLayout());
    }

    uint32_t push_constant_offset = 0;
    std::vector<VkPushConstantRange> packed_push_constants;
    packed_push_constants.reserve(push_constant_ranges_.size());

    for (auto range : push_constant_ranges_) {
        range.offset = push_constant_offset;
        push_constant_offset += range.size;
        packed_push_constants.push_back(range);
    }

    VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = static_cast<uint32_t>(set_layouts.size()),
        .pSetLayouts = set_layouts.data(),
        .pushConstantRangeCount = static_cast<uint32_t>(packed_push_constants.size()),
        .pPushConstantRanges = packed_push_constants.data(),
    };

    VkPipelineLayout native_layout = VK_NULL_HANDLE;
    VK_CHECK_ERROR(vkCreatePipelineLayout(device, &layout_info, nullptr, &native_layout));

    std::vector<VkPipelineShaderStageCreateInfo> shader_stages;
    shader_stages.reserve(shader_descriptions_.size());
    for (const auto &desc : shader_descriptions_) {
        shader_stages.push_back(
            VkPipelineShaderStageCreateInfo{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = static_cast<VkShaderStageFlagBits>(desc.stage),
                .module = native_shader_module,
                .pName = desc.entry_point.c_str(),
            });
    }

    VkComputePipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = shader_stages[0],
        .layout = native_layout,
    };

    VkPipeline native_pipeline = VK_NULL_HANDLE;
    VK_CHECK_ERROR(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &native_pipeline));

    auto pipeline = util::RefCountedPtr<ComputePipeline>{
        new ComputePipeline(scheduler_, native_shader_module, native_layout, native_pipeline)};
    pipeline->descriptor_set_layouts_ = descriptor_set_layouts_;

    return pipeline;
}

RenderPipelineBuilder::RenderPipelineBuilder(IResourceScheduler *scheduler)
    : scheduler_{scheduler}, vertex_input_desc_{std::nullopt},
      input_assembly_desc_{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
          .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
          .primitiveRestartEnable = VK_FALSE,
      },
      depth_stencil_desc_{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
          .depthTestEnable = VK_TRUE,
          .depthWriteEnable = VK_TRUE,
          .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
          .depthBoundsTestEnable = VK_FALSE,
          .stencilTestEnable = VK_FALSE,
      },
      rasterization_desc_{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
          .depthClampEnable = VK_FALSE,
          .rasterizerDiscardEnable = VK_FALSE,
          .polygonMode = VK_POLYGON_MODE_FILL,
          .cullMode = VK_CULL_MODE_BACK_BIT,
          .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
          .depthBiasEnable = VK_FALSE,
          .lineWidth = 1.0f,
      },
      multisample_desc_{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
          .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
          .sampleShadingEnable = VK_FALSE,
      } {}

auto RenderPipelineBuilder::blendAttachmentNoBlending() -> VkPipelineColorBlendAttachmentState {
    return VkPipelineColorBlendAttachmentState{
        .blendEnable = VK_FALSE,
        .colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
}

auto RenderPipelineBuilder::blendAttachmentAlphaBlending() -> VkPipelineColorBlendAttachmentState {
    return VkPipelineColorBlendAttachmentState{
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
}

auto RenderPipelineBuilder::withShaderStage(
    std::span<const uint32_t> bytecode, std::span<const ShaderDescription> descriptions) -> RenderPipelineBuilder & {
    shader_bytecode_.assign(bytecode.begin(), bytecode.end());
    shader_descriptions_.assign(descriptions.begin(), descriptions.end());

    return *this;
}

auto RenderPipelineBuilder::withDescriptorSet(util::RefCountedPtr<DescriptorLayout> set_layout)
    -> RenderPipelineBuilder & {
    descriptor_set_layouts_.push_back(std::move(set_layout));
    return *this;
}

auto RenderPipelineBuilder::withPushConstant(VkDeviceSize size, VkShaderStageFlags stages) -> RenderPipelineBuilder & {
    push_constant_ranges_.push_back(
        VkPushConstantRange{
            .stageFlags = stages,
            .offset = 0,
            .size = static_cast<uint32_t>(size),
        });

    return *this;
}

auto RenderPipelineBuilder::withPrimitiveTopology(VkPrimitiveTopology topology) -> RenderPipelineBuilder & {
    input_assembly_desc_.topology = topology;
    return *this;
}

auto RenderPipelineBuilder::withDepthAttachment(Format depth_format) -> RenderPipelineBuilder & {
    depth_attachment_format_ = depth_format;
    return *this;
}

auto RenderPipelineBuilder::addColorAttachment(
    Format color_format, VkPipelineColorBlendAttachmentState blend_attachment) -> RenderPipelineBuilder & {
    color_attachment_formats_.push_back(color_format);
    color_blend_attachments_.push_back(blend_attachment);

    return *this;
}

auto RenderPipelineBuilder::addColorAttachmentAlphaBlend(Format color_format) -> RenderPipelineBuilder & {
    return addColorAttachment(color_format, blendAttachmentAlphaBlending());
}

auto RenderPipelineBuilder::withRasterizationDesc(VkPipelineRasterizationStateCreateInfo desc)
    -> RenderPipelineBuilder & {
    rasterization_desc_ = desc;
    return *this;
}

auto RenderPipelineBuilder::withMultisampleDesc(VkPipelineMultisampleStateCreateInfo desc) -> RenderPipelineBuilder & {
    multisample_desc_ = desc;
    return *this;
}

auto RenderPipelineBuilder::withPolygonMode(VkPolygonMode mode) -> RenderPipelineBuilder & {
    rasterization_desc_.polygonMode = mode;
    return *this;
}

auto RenderPipelineBuilder::withCullMode(VkCullModeFlags mode) -> RenderPipelineBuilder & {
    rasterization_desc_.cullMode = mode;
    return *this;
}

auto RenderPipelineBuilder::withSampleCount(VkSampleCountFlagBits count) -> RenderPipelineBuilder & {
    multisample_desc_.rasterizationSamples = count;
    return *this;
}

auto RenderPipelineBuilder::build() -> util::RefCountedPtr<RenderPipeline> {
    VkDevice device = scheduler_->context()->device();

    VkShaderModuleCreateInfo shader_module_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = shader_bytecode_.size(),
        .pCode = reinterpret_cast<const uint32_t *>(shader_bytecode_.data()),
    };

    VkShaderModule native_shader_module = VK_NULL_HANDLE;
    VK_CHECK_ERROR(vkCreateShaderModule(device, &shader_module_info, nullptr, &native_shader_module));

    std::vector<VkDescriptorSetLayout> set_layouts;
    set_layouts.reserve(descriptor_set_layouts_.size());

    for (const auto &layout : descriptor_set_layouts_) {
        set_layouts.push_back(layout->nativeLayout());
    }

    uint32_t push_constant_offset = 0;
    std::vector<VkPushConstantRange> packed_push_constants;
    packed_push_constants.reserve(push_constant_ranges_.size());

    for (auto range : push_constant_ranges_) {
        range.offset = push_constant_offset;
        push_constant_offset += range.size;
        packed_push_constants.push_back(range);
    }

    VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = static_cast<uint32_t>(set_layouts.size()),
        .pSetLayouts = set_layouts.data(),
        .pushConstantRangeCount = static_cast<uint32_t>(packed_push_constants.size()),
        .pPushConstantRanges = packed_push_constants.data(),
    };

    VkPipelineLayout native_layout = VK_NULL_HANDLE;
    VK_CHECK_ERROR(vkCreatePipelineLayout(device, &layout_info, nullptr, &native_layout));

    std::vector<VkPipelineShaderStageCreateInfo> shader_stages;
    shader_stages.reserve(shader_descriptions_.size());
    for (const auto &desc : shader_descriptions_) {
        shader_stages.push_back(
            VkPipelineShaderStageCreateInfo{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = static_cast<VkShaderStageFlagBits>(desc.stage),
                .module = native_shader_module,
                .pName = desc.entry_point.c_str(),
            });
    }

    VkPipelineVertexInputStateCreateInfo vertex_input_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = vertex_input_desc_.has_value() ? uint32_t{1u} : uint32_t{0u},
        .pVertexBindingDescriptions = vertex_input_desc_.has_value() ? &vertex_input_desc_.value() : nullptr,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(vertex_input_attribs_.size()),
        .pVertexAttributeDescriptions = vertex_input_attribs_.data(),
    };

    std::vector<VkFormat> color_attachment_vk_formats;
    color_attachment_vk_formats.reserve(color_attachment_formats_.size());

    for (const auto &fmt : color_attachment_formats_) {
        color_attachment_vk_formats.push_back(formatToVk(fmt));
    }

    VkPipelineRenderingCreateInfo rendering_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = static_cast<uint32_t>(color_attachment_vk_formats.size()),
        .pColorAttachmentFormats = color_attachment_vk_formats.data(),
        .depthAttachmentFormat = depth_attachment_format_ ? formatToVk(*depth_attachment_format_) : VK_FORMAT_UNDEFINED,
    };

    VkPipelineColorBlendStateCreateInfo color_blend_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .attachmentCount = static_cast<uint32_t>(color_blend_attachments_.size()),
        .pAttachments = color_blend_attachments_.data(),
    };

    VkPipelineViewportStateCreateInfo viewport_desc = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    std::array<VkDynamicState, 2> dynamic_states = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state_desc = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(dynamic_states.size()),
        .pDynamicStates = dynamic_states.data(),
    };

    VkGraphicsPipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering_info,
        .stageCount = static_cast<uint32_t>(shader_stages.size()),
        .pStages = shader_stages.data(),
        .pVertexInputState = &vertex_input_info,
        .pInputAssemblyState = &input_assembly_desc_,
        .pViewportState = &viewport_desc,
        .pRasterizationState = &rasterization_desc_,
        .pMultisampleState = &multisample_desc_,
        .pDepthStencilState = depth_attachment_format_ ? &depth_stencil_desc_ : nullptr,
        .pColorBlendState = &color_blend_info,
        .pDynamicState = &dynamic_state_desc,
        .layout = native_layout,
    };

    VkPipeline native_pipeline = VK_NULL_HANDLE;
    VK_CHECK_ERROR(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &native_pipeline));

    auto pipeline = util::RefCountedPtr<RenderPipeline>{
        new RenderPipeline(scheduler_, native_shader_module, native_layout, native_pipeline)};
    pipeline->descriptor_set_layouts_ = descriptor_set_layouts_;

    return pipeline;
}

} // namespace graphics