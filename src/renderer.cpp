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
            .size = VK_WHOLE_SIZE,
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
    Renderer *renderer, std::span<const uint8_t> vertex_buffer, uint32_t num_vertices, std::span<uint32_t> indices)
    -> std::unique_ptr<Mesh> {
    std::unique_ptr<Mesh> mesh{new (std::nothrow) Mesh()};
    if (!mesh) {
        LogError("vulkan: failed to allocate mesh object");
        return nullptr;
    }

    const auto &memory = mesh->renderer_->context_->memory();
    mesh->renderer_ = renderer;

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

    const auto surface_extent = renderer->context_->surfaceExtent();
    VkImageCreateInfo depth_buffer_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = renderer->context_->supportedDepthFormat(),
        .extent = {surface_extent.width, surface_extent.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    renderer->depth_buffer_ = renderer->context_->memory().createImage(depth_buffer_info);
    renderer->depth_buffer_view_ = renderer->depth_buffer_.createView(
        VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_D24_UNORM_S8_UINT, VK_IMAGE_ASPECT_DEPTH_BIT);

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

    const auto num_swapchain_images = renderer->context_->swapchainImages().size();

    renderer->swapchain_data_.resize(num_swapchain_images);
    for (size_t i = 0; i < num_swapchain_images; ++i) {
        VK_CHECK_ERROR(vkCreateSemaphore(
            renderer->context_->device(), &semaphore_desc, nullptr, &renderer->swapchain_data_[i].render_semaphore));
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

    renderer->geometry_pass_descriptors_ = DescriptorSetHelper::create(renderer.get(), per_frame_desc);

    const auto shader_buffer = description.shader_loader->loadGeometryPassShader();
    if (!shader_buffer) {
        LogError("vulkan: renderer cannot load main shader");
        return nullptr;
    }

    VkShaderModuleCreateInfo shader_module_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = shader_buffer->size() * sizeof(uint32_t),
        .pCode = shader_buffer->data(),
    };

    VK_CHECK_ERROR(vkCreateShaderModule(
        renderer->context_->device(), &shader_module_info, nullptr, &renderer->geometry_pass_shader_));

    return renderer;
}

Renderer::~Renderer() noexcept {
    LogInfo("vulkan: releasing renderer resources");

    if (VK_NULL_HANDLE != geometry_pass_shader_) {
        vkDestroyShaderModule(context_->device(), geometry_pass_shader_, nullptr);
        geometry_pass_shader_ = VK_NULL_HANDLE;
    }

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
