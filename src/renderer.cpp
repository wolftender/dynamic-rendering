#include "renderer.hpp"
#include "logger.hpp"

#include <vulkan/vulkan.h>
#include <volk.h>
#include <vk_mem_alloc.h>
#include <vulkan/vk_enum_string_helper.h>

namespace graphics {

template <typename cbBufferDataType> class Renderer::SceneBufferHelper final {
public:
    static constexpr auto kDataSize = sizeof(cbBufferDataType);

    static auto create(Renderer *renderer) -> std::unique_ptr<SceneBufferHelper<cbBufferDataType>> {
        std::unique_ptr<SceneBufferHelper<cbBufferDataType>> buffer_helper{new (std::nothrow)
                                                                               SceneBufferHelper<cbBufferDataType>()};

        if (!buffer_helper) {
            LogError("vulkan: failed to allocate buffer helper");
            return nullptr;
        }

        buffer_helper->renderer_ = renderer;
        buffer_helper->context_ = buffer_helper->renderer_->context_;

        buffer_helper->device_buffer_ = buffer_helper->context_->memory().createDeviceBuffer(
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, kDataSize);
        buffer_helper->staging_buffer_ =
            buffer_helper->context_->memory().createSharedBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, kDataSize);

        VkBufferDeviceAddressInfo addr_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
            .buffer = buffer_helper->device_buffer_.buffer(),
        };

        buffer_helper->device_address_ = vkGetBufferDeviceAddress(buffer_helper->context_->device(), &addr_info);
        ::memset(buffer_helper->staging_buffer_.cpuMappedPointer(), 0, kDataSize);

        return buffer_helper;
    }

    ~SceneBufferHelper() = default;

    SceneBufferHelper(const SceneBufferHelper &) = delete;
    auto operator=(const SceneBufferHelper &) = delete;

    SceneBufferHelper(SceneBufferHelper &&) noexcept = delete;
    auto operator=(SceneBufferHelper &&) noexcept = delete;

    auto stagingBuffer() const -> const Buffer & { return staging_buffer_; }
    auto deviceBuffer() const -> const Buffer & { return device_buffer_; }
    auto deviceAddress() const -> VkDeviceAddress { return device_address_; }

    auto storage() -> cbBufferDataType & { return data_; }
    auto storage() const -> const cbBufferDataType & { return data_; }

    auto upload(VkCommandBuffer command_buffer) -> void {
        // copy to staging buffer
        ::memcpy(staging_buffer_.cpuMappedPointer(), reinterpret_cast<const void *>(&data_), kDataSize);

        // gpu buffer don't care -> transfer dst
        VkBufferMemoryBarrier2 barrier_unknown_to_transfer_dst = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = 0,
            .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .buffer = device_buffer_.buffer(),
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        };

        VkDependencyInfo dep_unknown_to_transfer_dst = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .bufferMemoryBarrierCount = 1,
            .pBufferMemoryBarriers = &barrier_unknown_to_transfer_dst,
        };

        vkCmdPipelineBarrier2(command_buffer, &dep_unknown_to_transfer_dst);

        // copy
        VkBufferCopy buffer_copy = {
            .srcOffset = 0,
            .dstOffset = 0,
            .size = kDataSize,
        };

        vkCmdCopyBuffer(command_buffer, staging_buffer_.buffer(), device_buffer_.buffer(), 1, &buffer_copy);

        // gpu buffer transfer dst -> shader read
        VkBufferMemoryBarrier2 barrier_transfer_dst_to_shader_read = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
            .buffer = device_buffer_.buffer(),
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        };

        VkDependencyInfo dep_transfer_dst_to_shader_read = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .bufferMemoryBarrierCount = 1,
            .pBufferMemoryBarriers = &barrier_transfer_dst_to_shader_read,
        };

        vkCmdPipelineBarrier2(command_buffer, &dep_transfer_dst_to_shader_read);
    }

private:
    SceneBufferHelper() = default;

    cbBufferDataType data_;

    Buffer staging_buffer_ = {};
    Buffer device_buffer_ = {};
    VkDeviceAddress device_address_ = {};

    Context *context_ = nullptr;
    Renderer *renderer_ = nullptr;
};

auto Renderer::Mesh::create(
    Renderer *renderer, std::span<const uint8_t> vertex_buffer, uint32_t num_vertices,
    std::span<const uint32_t> indices) -> std::unique_ptr<Mesh> {
    std::unique_ptr<Mesh> mesh{new (std::nothrow) Mesh()};
    if (!mesh) {
        LogError("vulkan: failed to allocate mesh object");
        return nullptr;
    }

    mesh->renderer_ = renderer;
    const auto &memory = mesh->renderer_->context_->memory();

    mesh->num_vertices_ = num_vertices;
    mesh->num_indices_ = std::size(indices);

    mesh->vertex_buffer_size_ = vertex_buffer.size();
    mesh->index_buffer_size_ = indices.size() * sizeof(uint32_t);

    mesh->vertex_buffer_ = memory.createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertex_buffer);
    mesh->index_bufer_ = memory.createBuffer(
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        std::span<const uint8_t>{reinterpret_cast<const uint8_t *>(indices.data()), mesh->index_buffer_size_});

    return mesh;
}

auto Renderer::Texture::fromRgba(Renderer *renderer, const Description &desc, std::span<const uint8_t> rgba_data)
    -> std::unique_ptr<Texture> {
    std::unique_ptr<Texture> texture{new (std::nothrow) Texture()};
    if (!texture) {
        LogError("vulkan: failed to allocate texture");
        return nullptr;
    }

    texture->renderer_ = renderer;
    texture->description_ = desc;

    VkFilter min_filter, mag_filter;
    switch (desc.min_filter) {
    case MinFilter::eLinear:
        min_filter = VK_FILTER_LINEAR;
        break;
    case MinFilter::eNearest:
        min_filter = VK_FILTER_NEAREST;
        break;
    default:
        LogError("vulkan: invalid texture min filter");
        return nullptr;
    }

    switch (desc.mag_filter) {
    case MagFilter::eLinear:
        mag_filter = VK_FILTER_LINEAR;
        break;
    case MagFilter::eNearest:
        mag_filter = VK_FILTER_NEAREST;
        break;
    default:
        LogError("vulkan: invalid texture min filter");
        return nullptr;
    }

    texture->image_ = texture->renderer_->context_->memory().createImageRgba(
        VK_IMAGE_USAGE_SAMPLED_BIT, {desc.width, desc.height}, rgba_data);
    texture->image_view_ =
        texture->image().createView(VK_IMAGE_VIEW_TYPE_2D, texture->image().format(), VK_IMAGE_ASPECT_COLOR_BIT);

    VkSamplerCreateInfo sampler_desc = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = mag_filter,
        .minFilter = min_filter,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .anisotropyEnable = VK_TRUE,
        .maxAnisotropy = 8.0f,
        .maxLod = 1,
    };

    VK_CHECK_ERROR(vkCreateSampler(texture->renderer_->context_->device(), &sampler_desc, nullptr, &texture->sampler_));
    return texture;
}

Renderer::Texture::~Texture() noexcept {
    if (VK_NULL_HANDLE != sampler_) {
        vkDestroySampler(renderer_->context_->device(), sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }
}

Renderer::TexturePool::TexturePool() {
    for (uint32_t i = 0; i < kNumTexturePoolSize; ++i) {
        free_list_.push_back(i);
        descriptors_[i].imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        descriptors_[i].imageView = VK_NULL_HANDLE;
        descriptors_[i].sampler = VK_NULL_HANDLE;
        textures_[i] = nullptr;
    }
}

auto Renderer::TexturePool::get(uint32_t handle) const -> const Texture * {
    if (handle > textures_.size() || !textures_[handle]) {
        return nullptr;
    }

    return textures_[handle].get();
}

auto Renderer::TexturePool::insert(std::unique_ptr<Texture> texture) -> std::optional<uint32_t> {
    if (free_list_.empty()) {
        return std::nullopt;
    }

    auto slot = free_list_.front();
    free_list_.pop_front();

    textures_[slot] = std::move(texture);
    const auto &t = *textures_[slot].get();

    descriptors_[slot].imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
    descriptors_[slot].imageView = t.imageView().view();
    descriptors_[slot].sampler = t.sampler();

    return slot;
}

auto Renderer::TexturePool::erase(uint32_t handle) -> std::unique_ptr<Texture> {
    if (handle > textures_.size() || !textures_[handle]) {
        return nullptr;
    }

    descriptors_[handle].imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    descriptors_[handle].imageView = VK_NULL_HANDLE;
    descriptors_[handle].sampler = VK_NULL_HANDLE;

    auto texture = std::move(textures_[handle]);
    free_list_.push_front(handle);

    return texture;
}

Renderer::TexturePool::~TexturePool() noexcept {}

Renderer::MeshPool::MeshPool() {
    for (uint32_t i = 0; i < kNumMeshPoolSize; ++i) {
        free_list_.push_back(i);
        meshes_[i] = nullptr;
    }
}

auto Renderer::MeshPool::get(uint32_t handle) const -> const Mesh * {
    if (handle > meshes_.size() || !meshes_[handle]) {
        return nullptr;
    }

    return meshes_[handle].get();
}

auto Renderer::MeshPool::insert(std::unique_ptr<Mesh> mesh) -> std::optional<uint32_t> {
    if (free_list_.empty()) {
        return std::nullopt;
    }

    auto slot = free_list_.front();
    free_list_.pop_front();

    meshes_[slot] = std::move(mesh);
    return slot;
}

auto Renderer::MeshPool::erase(uint32_t handle) -> std::unique_ptr<Mesh> {
    if (handle > meshes_.size() || !meshes_[handle]) {
        return nullptr;
    }

    auto mesh = std::move(meshes_[handle]);
    free_list_.push_front(handle);

    return mesh;
}

Renderer::MeshPool::~MeshPool() noexcept {}

auto Renderer::DescriptorSetHelper::create(Renderer *renderer, const Description &description)
    -> std::unique_ptr<DescriptorSetHelper> {
    std::unique_ptr<DescriptorSetHelper> helper{new (std::nothrow) DescriptorSetHelper()};
    if (!helper) {
        LogError("vulkan: cannot allocate descriptor set helper");
        return nullptr;
    }

    helper->renderer_ = renderer;
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
                .descriptorCount = num_sampler_textures * kNumFramesInFlight,
            });
    }

    if (num_storage_buffers > 0) {
        pool_sizes.push_back(
            VkDescriptorPoolSize{
                .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = num_storage_buffers * kNumFramesInFlight,
            });
    }

    if (num_uniform_buffers > 0) {
        pool_sizes.push_back(
            VkDescriptorPoolSize{
                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = num_uniform_buffers * kNumFramesInFlight,
            });
    }

    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = kNumFramesInFlight,
        .poolSizeCount = static_cast<uint32_t>(pool_sizes.size()),
        .pPoolSizes = pool_sizes.data(),
    };

    VK_CHECK_ERROR(vkCreateDescriptorPool(device, &pool_info, nullptr, &helper->pool_));

    // allocate the descriptor sets
    std::array<uint32_t, kNumFramesInFlight> desc_counts;
    for (size_t i = 0; i < kNumFramesInFlight; ++i) {
        desc_counts[i] = variable_desc_max_count;
    }

    VkDescriptorSetVariableDescriptorCountAllocateInfo variable_desc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
        .descriptorSetCount = kNumFramesInFlight,
        .pDescriptorCounts = desc_counts.data(),
    };

    // https://github.com/KhronosGroup/Vulkan-Docs/issues/1236
    std::array<VkDescriptorSetLayout, kNumFramesInFlight> layouts;
    for (size_t i = 0; i < kNumFramesInFlight; ++i) {
        layouts[i] = helper->layout_;
    }

    VkDescriptorSetAllocateInfo set_alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = (variable_desc_max_count > 0) ? &variable_desc_info : nullptr,
        .descriptorPool = helper->pool_,
        .descriptorSetCount = kNumFramesInFlight,
        .pSetLayouts = layouts.data(),
    };

    VK_CHECK_ERROR(vkAllocateDescriptorSets(device, &set_alloc_info, helper->sets_.data()));
    return helper;
}

Renderer::DescriptorSetHelper::~DescriptorSetHelper() noexcept {
    const auto device = renderer_->context_->device();

    if (VK_NULL_HANDLE != layout_) {
        vkDestroyDescriptorSetLayout(device, layout_, nullptr);
        layout_ = VK_NULL_HANDLE;
    }

    if (VK_NULL_HANDLE != pool_) {
        vkDestroyDescriptorPool(device, pool_, nullptr);
        pool_ = VK_NULL_HANDLE;
    }
}

auto Renderer::create(const Description &description) -> std::unique_ptr<Renderer> {
    std::unique_ptr<Renderer> renderer{new (std::nothrow) Renderer()};
    if (!renderer) {
        LogError("vulkan: cannot allocate renderer object");
        return {};
    }

    renderer->context_ = description.context;
    renderer->createSwapchainData();

    for (size_t i = 0; i < kNumFramesInFlight; ++i) {
        renderer->frames_[i].scene_buffer = SceneBufferHelper<cbFrameHeapBuffer>::create(renderer.get());
    }

    // allocate command buffers
    VkCommandPoolCreateInfo command_pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = renderer->context_->graphicsQueueFamily(),
    };

    VK_CHECK_ERROR(
        vkCreateCommandPool(renderer->context_->device(), &command_pool_info, nullptr, &renderer->command_pool_));

    VkCommandBufferAllocateInfo command_buffers_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = renderer->command_pool_,
        .commandBufferCount = kNumFramesInFlight,
    };

    std::array<VkCommandBuffer, kNumFramesInFlight> command_buffers;

    VK_CHECK_ERROR(
        vkAllocateCommandBuffers(renderer->context_->device(), &command_buffers_info, command_buffers.data()));

    for (size_t i = 0; i < kNumFramesInFlight; ++i) {
        renderer->frames_[i].command_buffer = command_buffers[i];
    }

    VkSemaphoreCreateInfo semaphore_desc = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };

    VkFenceCreateInfo fence_desc = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    // synchronization structures
    for (size_t i = 0; i < kNumFramesInFlight; ++i) {
        auto &frame = renderer->frames_[i];
        VK_CHECK_ERROR(vkCreateFence(renderer->context_->device(), &fence_desc, nullptr, &frame.fence));
        VK_CHECK_ERROR(
            vkCreateSemaphore(renderer->context_->device(), &semaphore_desc, nullptr, &frame.present_semaphore));
    }

    // descriptor sets
    const DescriptorSetHelper::Description per_frame_desc = {
        .layout =
            {
                // PerFrameTexturePool
                DescriptorSetHelper::DescriptorDescription{
                    .type = DescriptorSetHelper::DescriptorDataType::eSamplerTexture,
                    .num_bindings = kNumTexturePoolSize,
                },
            },
    };

    renderer->descriptor_helper_ = DescriptorSetHelper::create(renderer.get(), per_frame_desc);

    renderer->geometry_pass_ = OpaqueGeometryPass::create(renderer.get(), description.shader_loader.get());
    if (!renderer->geometry_pass_) {
        LogError("vulkan: renderer cannot initialize graphics pipeline");
        return nullptr;
    }

    return renderer;
}

auto Renderer::createSwapchainData() -> void {
    const auto surface_extent = context_->surfaceExtent();
    VkImageCreateInfo depth_buffer_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = context_->supportedDepthFormat(),
        .extent = {surface_extent.width, surface_extent.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    depth_buffer_ = context_->memory().createImage(depth_buffer_info);
    depth_buffer_view_ =
        depth_buffer_.createView(VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_D24_UNORM_S8_UINT, VK_IMAGE_ASPECT_DEPTH_BIT);

    VkSemaphoreCreateInfo semaphore_desc = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };

    const auto num_swapchain_images = context_->swapchainImages().size();

    for (auto &image_data : swapchain_data_) {
        if (VK_NULL_HANDLE != image_data.render_semaphore) {
            vkDestroySemaphore(context_->device(), image_data.render_semaphore, nullptr);
            image_data.render_semaphore = VK_NULL_HANDLE;
        }
    }

    swapchain_data_.clear();
    swapchain_data_.resize(num_swapchain_images);

    for (size_t i = 0; i < num_swapchain_images; ++i) {
        VK_CHECK_ERROR(
            vkCreateSemaphore(context_->device(), &semaphore_desc, nullptr, &swapchain_data_[i].render_semaphore));
    }
}

auto Renderer::OpaqueGeometryPass::create(Renderer *renderer, const IShaderLoader *shader_loader)
    -> std::unique_ptr<OpaqueGeometryPass> {
    std::unique_ptr<OpaqueGeometryPass> pass{new (std::nothrow) OpaqueGeometryPass()};
    if (!pass) {
        LogError("vulkan: renderer cannot allocate opaque geometry pass resources");
        return nullptr;
    }

    pass->renderer_ = renderer;

    const auto context = pass->renderer_->context_;
    const auto device = pass->renderer_->context_->device();

    const auto shader_buffer = shader_loader->loadGeometryPassShader();
    if (!shader_buffer) {
        LogError("vulkan: renderer cannot load main shader");
        return nullptr;
    }

    VkShaderModuleCreateInfo shader_module_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = shader_buffer->size() * sizeof(uint32_t),
        .pCode = shader_buffer->data(),
    };

    VK_CHECK_ERROR(vkCreateShaderModule(device, &shader_module_info, nullptr, &pass->shader_module_));

    // graphics pipeline
    VkPushConstantRange push_constant_range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .size = sizeof(cbPushConstantBuffer),
    };

    VkDescriptorSetLayout descriptor_layout = pass->renderer_->descriptor_helper_->layout();

    VkPipelineLayoutCreateInfo pipeline_layout_desc = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &descriptor_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constant_range,
    };

    VK_CHECK_ERROR(vkCreatePipelineLayout(device, &pipeline_layout_desc, nullptr, &pass->pipeline_layout_));

    VkVertexInputBindingDescription vertex_binding = {
        .binding = 0,
        .stride = sizeof(StaticVertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };

    std::array<VkVertexInputAttributeDescription, 5> vertex_attribs = {
        VkVertexInputAttributeDescription{
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(StaticVertex, position)},
        VkVertexInputAttributeDescription{
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(StaticVertex, normal)},
        VkVertexInputAttributeDescription{
            .location = 2, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(StaticVertex, uv)},
        VkVertexInputAttributeDescription{
            .location = 3, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(StaticVertex, color)},
        VkVertexInputAttributeDescription{
            .location = 4,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .offset = offsetof(StaticVertex, tangent)},
    };

    VkPipelineVertexInputStateCreateInfo vertex_input_desc = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &vertex_binding,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(vertex_attribs.size()),
        .pVertexAttributeDescriptions = vertex_attribs.data(),
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly_desc = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };

    std::array<VkPipelineShaderStageCreateInfo, 2> shader_stage_desc = {
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = pass->shader_module_,
            .pName = "vsMain",
        },
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = pass->shader_module_,
            .pName = "fsMain",
        },
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

    VkPipelineDepthStencilStateCreateInfo depth_stencil_desc = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
    };

    const auto swapchain_format = context->swapchainFormat().format;
    VkPipelineRenderingCreateInfo rendering_desc = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &swapchain_format,
        .depthAttachmentFormat = context->supportedDepthFormat(),
    };

    VkPipelineColorBlendAttachmentState blend_attachment_desc = {
        .colorWriteMask = 0xf,
    };

    VkPipelineColorBlendStateCreateInfo color_blend_desc = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blend_attachment_desc,
    };

    VkPipelineRasterizationStateCreateInfo rasterization_desc = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .lineWidth = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo multisample_desc = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    VkGraphicsPipelineCreateInfo pipeline_desc = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering_desc,
        .stageCount = static_cast<uint32_t>(shader_stage_desc.size()),
        .pStages = shader_stage_desc.data(),
        .pVertexInputState = &vertex_input_desc,
        .pInputAssemblyState = &input_assembly_desc,
        .pViewportState = &viewport_desc,
        .pRasterizationState = &rasterization_desc,
        .pMultisampleState = &multisample_desc,
        .pDepthStencilState = &depth_stencil_desc,
        .pColorBlendState = &color_blend_desc,
        .pDynamicState = &dynamic_state_desc,
        .layout = pass->pipeline_layout_,
    };

    VK_CHECK_ERROR(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_desc, nullptr, &pass->pipeline_));
    return pass;
}

Renderer::OpaqueGeometryPass::~OpaqueGeometryPass() noexcept {
    if (VK_NULL_HANDLE != pipeline_) {
        vkDestroyPipeline(renderer_->context_->device(), pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }

    if (VK_NULL_HANDLE != pipeline_layout_) {
        vkDestroyPipelineLayout(renderer_->context_->device(), pipeline_layout_, nullptr);
        pipeline_layout_ = VK_NULL_HANDLE;
    }

    if (VK_NULL_HANDLE != shader_module_) {
        vkDestroyShaderModule(renderer_->context_->device(), shader_module_, nullptr);
        shader_module_ = VK_NULL_HANDLE;
    }
}

auto Renderer::frame() -> util::Result {
    if (swapchain_needs_update_) {
        if (!pending_resize_.has_value()) {
            return util::Result::eSuccess;
        }

        LogInfo("vulkan: renderer will trigger swapchain resize");

        context_->resize(pending_resize_->surface_extent, pending_resize_->framebuffer_extent);
        createSwapchainData();

        swapchain_needs_update_ = false;
        pending_resize_.reset();
    }

    auto &current_frame = getCurrentFrame();

    camera_.setAspect(
        static_cast<float>(context_->framebufferExtent().width) /
        static_cast<float>(context_->framebufferExtent().height));

    VK_CHECK_ERROR(vkWaitForFences(context_->device(), 1, &current_frame.fence, VK_TRUE, UINT64_MAX));
    VK_CHECK_ERROR(vkResetFences(context_->device(), 1, &current_frame.fence));

    uint32_t image_index = 0;
    {
        VkResult res = vkAcquireNextImageKHR(
            context_->device(), context_->swapchain(), UINT64_MAX, current_frame.present_semaphore, VK_NULL_HANDLE,
            &image_index);

        switch (res) {
        case VK_SUCCESS:
        case VK_SUBOPTIMAL_KHR:
            break;
        case VK_ERROR_OUT_OF_DATE_KHR:
            LogInfo("vulkan: renderer awaiting resize event");
            swapchain_needs_update_ = true;
            break;
        default:
            LogError("cannot acquire next swapchain image: {}", string_VkResult(res));
            return util::Result::eFailure;
        }
    }

    // update frame data
    auto &scene_buffer = current_frame.scene_buffer;
    auto &scene_buffer_data = scene_buffer->storage();

    scene_buffer_data.projection = camera_.projection();
    scene_buffer_data.projection_inv = camera_.projectionInv();
    scene_buffer_data.view = camera_.view();
    scene_buffer_data.view_inv = camera_.viewInv();

    for (uint32_t i = 0; i < draw_queue_fill_; ++i) {
        scene_buffer_data.static_objects[i].world = draw_queue_[i]->world_matrix;
    }

    auto command_buffer = current_frame.command_buffer;
    VK_CHECK_ERROR(vkResetCommandBuffer(command_buffer, 0));

    VkCommandBufferBeginInfo command_buffer_begin_desc = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    VK_CHECK_ERROR(vkBeginCommandBuffer(command_buffer, &command_buffer_begin_desc));

    // upload buffer data - this also inserts a memory barrier!!
    scene_buffer->upload(command_buffer);

    std::array<VkImageMemoryBarrier2, 2> output_barriers = {
        VkImageMemoryBarrier2{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = 0,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .image = context_->swapchainImages()[image_index],
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1},
        },
        VkImageMemoryBarrier2{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .image = depth_buffer_.image(),
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
                    .levelCount = 1,
                    .layerCount = 1,
                },
        },
    };

    VkDependencyInfo output_dependency_desc = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = static_cast<uint32_t>(output_barriers.size()),
        .pImageMemoryBarriers = output_barriers.data(),
    };

    vkCmdPipelineBarrier2(command_buffer, &output_dependency_desc);

    VkRenderingAttachmentInfo color_attachment_desc = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = context_->swapchainImageViews()[image_index],
        .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {.color = {{0.207f, 0.36f, 0.64f, 1.0f}}},
    };

    VkRenderingAttachmentInfo depth_attachment_desc = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = depth_buffer_view_.view(),
        .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue = {.depthStencil = {1.0f, 0}},
    };

    VkRenderingInfo rendering_info = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea =
            {
                .extent = context_->framebufferExtent(),
            },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment_desc,
        .pDepthAttachment = &depth_attachment_desc,
    };

    vkCmdBeginRendering(command_buffer, &rendering_info);

    // VK_HR_maintenance1, core from Vulkan 1.1, we can flip y to be "opengl-friendly"
    VkViewport vp = {
        .x = 0.0f,
        .y = static_cast<float>(context_->surfaceExtent().height),
        .width = static_cast<float>(context_->surfaceExtent().width),
        .height = -static_cast<float>(context_->surfaceExtent().height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };

    VkRect2D scissor = {.extent = context_->surfaceExtent()};

    vkCmdSetViewport(command_buffer, 0, 1, &vp);
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, geometry_pass_->pipeline());

    VkDescriptorSet ds = descriptor_helper_->getSetForFrame(current_frame_);
    vkCmdBindDescriptorSets(
        command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, geometry_pass_->pipelineLayout(), 0, 1, &ds, 0, nullptr);

    VkDeviceSize vertex_offset = 0;
    OpaqueGeometryPass::cbPushConstantBuffer push_constants;

    for (uint32_t i = 0; i < draw_queue_fill_; ++i) {
        const auto &draw_call = draw_queue_[i].value();
        withMesh(draw_call.mesh, [&](const Mesh &mesh) {
            vkCmdBindVertexBuffers(command_buffer, 0, 1, mesh.vertexBuffer().addrOf(), &vertex_offset);
            vkCmdBindIndexBuffer(command_buffer, mesh.indexBuffer().buffer(), 0, VK_INDEX_TYPE_UINT32);

            push_constants.frame_heap = scene_buffer->deviceAddress();
            push_constants.object_id = i;

            vkCmdPushConstants(
                command_buffer, geometry_pass_->pipelineLayout(),
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                sizeof(OpaqueGeometryPass::cbPushConstantBuffer), &push_constants);

            vkCmdDrawIndexed(command_buffer, mesh.numIndices(), 1, 0, 0, 0);
        });
    }

    vkCmdEndRendering(command_buffer);

    VkImageMemoryBarrier2 barrier_present = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = 0,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .image = context_->swapchainImages()[image_index],
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1},
    };

    VkDependencyInfo dependency_present = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier_present,
    };

    vkCmdPipelineBarrier2(command_buffer, &dependency_present);
    vkEndCommandBuffer(command_buffer);

    draw_queue_fill_ = 0;

    VkPipelineStageFlags wait_stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &current_frame.present_semaphore,
        .pWaitDstStageMask = &wait_stages,
        .commandBufferCount = 1,
        .pCommandBuffers = &command_buffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &swapchain_data_[image_index].render_semaphore,
    };

    VK_CHECK_ERROR(vkQueueSubmit(context_->graphicsQueue(), 1, &submit_info, current_frame.fence));
    current_frame_ = (current_frame_ + 1) % kNumFramesInFlight;

    VkSwapchainKHR swapchain = context_->swapchain();
    VkPresentInfoKHR present_info = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &swapchain_data_[image_index].render_semaphore,
        .swapchainCount = 1,
        .pSwapchains = &swapchain,
        .pImageIndices = &image_index,
    };

    {
        VkResult res = vkQueuePresentKHR(context_->presentQueue(), &present_info);

        switch (res) {
        case VK_SUCCESS:
        case VK_SUBOPTIMAL_KHR:
            break;
        case VK_ERROR_OUT_OF_DATE_KHR:
            LogInfo("vulkan: renderer awaiting resize event");
            swapchain_needs_update_ = true;
            break;
        default:
            LogError("cannot present swapchain image: {}", string_VkResult(res));
            return util::Result::eFailure;
        }
    }

    return util::Result::eSuccess;
}

Renderer::~Renderer() noexcept {
    LogInfo("vulkan: releasing renderer resources");
    VK_CHECK_ERROR(vkDeviceWaitIdle(context_->device()));

    if (VK_NULL_HANDLE != command_pool_) {
        vkDestroyCommandPool(context_->device(), command_pool_, nullptr);
        command_pool_ = VK_NULL_HANDLE;
    }

    for (size_t i = 0; i < kNumFramesInFlight; ++i) {
        auto &frame = frames_[i];

        if (VK_NULL_HANDLE != frame.fence) {
            vkDestroyFence(context_->device(), frame.fence, nullptr);
            frame.fence = VK_NULL_HANDLE;
        }

        if (VK_NULL_HANDLE != frame.present_semaphore) {
            vkDestroySemaphore(context_->device(), frame.present_semaphore, nullptr);
            frame.present_semaphore = VK_NULL_HANDLE;
        }
    }

    for (auto &image_data : swapchain_data_) {
        if (VK_NULL_HANDLE != image_data.render_semaphore) {
            vkDestroySemaphore(context_->device(), image_data.render_semaphore, nullptr);
            image_data.render_semaphore = VK_NULL_HANDLE;
        }
    }
}

} // namespace graphics
