#include "renderer.hpp"
#include "logger.hpp"

#include <vulkan/vulkan.h>
#include <volk.h>
#include <vk_mem_alloc.h>
#include <vulkan/vk_enum_string_helper.h>

namespace graphics {

template <uint32_t kNumSets> class Renderer::DescriptorSetHelper final {
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

    static auto create(Renderer *renderer, const Description &description) -> std::unique_ptr<DescriptorSetHelper> {
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

    ~DescriptorSetHelper() noexcept {
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

    DescriptorSetHelper(const DescriptorSetHelper &) = delete;
    auto operator=(const DescriptorSetHelper &) = delete;

    DescriptorSetHelper(DescriptorSetHelper &&) noexcept = delete;
    auto operator=(DescriptorSetHelper &&) noexcept = delete;

    auto pool() const -> VkDescriptorPool { return pool_; }
    auto description() const -> const Description & { return desc_; }
    auto layout() const -> VkDescriptorSetLayout { return layout_; }
    auto getSetForFrame(uint32_t frame) const -> VkDescriptorSet { return sets_[frame]; }

private:
    DescriptorSetHelper() = default;

    Renderer *renderer_ = nullptr;
    Description desc_;

    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;

    std::array<VkDescriptorSet, kNumFramesInFlight> sets_;
};

template <typename T, typename Tag, uint32_t kPoolSize> class Renderer::ResourcePool {
public:
    using Id = ResourceId<T, Tag>;

    ResourcePool() {
        for (uint32_t i = 0; i < kPoolSize; ++i) {
            storage_[i].resource = std::nullopt;
            storage_[i].valid = false;
            storage_[i].generation = 0;
            storage_[i].identifier = i;
            free_ids_.push(i);
        }
    }

    ~ResourcePool() = default;

    ResourcePool(const ResourcePool &) = delete;
    auto operator=(const ResourcePool &) = delete;

    ResourcePool(ResourcePool &&) noexcept = delete;
    auto operator=(ResourcePool &&) noexcept = delete;

    auto storeResource(T resource, uint32_t frame) -> std::optional<Id> {
        auto index = free_ids_.pop();
        if (!index.has_value()) {
            return std::nullopt;
        }

        auto &slot = storage_[index.value()];
        auto id = Id{index.value(), slot.generation};

        slot.resource = std::move(resource);
        slot.valid = true;
        slot.last_frame_id = frame;

        return id;
    }

    auto getResource(const Id &id) -> T * {
        auto &slot = storage_[id.index()];
        if (id.generation() != slot.generation) {
            return nullptr;
        }

        if (!slot.valid || !slot.resource.has_value()) {
            return nullptr;
        }

        return &slot.resource.value();
    }

    auto getResource(const Id &id) const -> const T * {
        auto &slot = storage_[id.index()];
        if (id.generation() != slot.generation) {
            return nullptr;
        }

        if (!slot.valid || !slot.resource.has_value()) {
            return nullptr;
        }

        return &slot.resource.value();
    }

    auto refResource(const Id id, uint32_t frame) -> const T * {
        auto &slot = storage_[id.index()];
        if (id.generation() != slot.generation) {
            return nullptr;
        }

        if (!slot.valid || !slot.resource.has_value()) {
            return nullptr;
        }

        slot.last_frame_id = frame;
        return &slot.resource.value();
    }

    auto destroyResource(const Id &id) -> void {
        auto &slot = storage_[id.index()];
        if (id.generation() != slot.generation) {
            return;
        }

        if (!slot.valid || !slot.resource.has_value()) {
            return;
        }

        slot.valid = false;
        slot.generation++;
        deletion_queues_[slot.last_frame_id].push(id.index());
    }

    auto garbageCollect(uint32_t last_frame) -> void {
        garbageCollect(last_frame, []([[maybe_unused]] auto id, [[maybe_unused]] auto &&element) {});
    }

    template <std::invocable<uint32_t, T &&> F> auto garbageCollect(uint32_t last_frame, F consumer) -> void {
        auto &queue = deletion_queues_[last_frame];
        while (!queue.empty()) {
            auto index = queue.pop();
            auto &slot = storage_[index.value()];

            consumer(index.value(), std::move(slot.resource.value()));
            slot.resource.reset();
        }
    }

private:
    struct Slot {
        std::optional<T> resource;
        uint32_t identifier;
        uint32_t generation;
        uint32_t last_frame_id;
        bool valid;
    };

    std::array<Slot, kPoolSize> storage_;
    util::FixedSizeQueue<uint32_t, kPoolSize> free_ids_;
    std::array<util::FixedSizeQueue<uint32_t, kPoolSize>, kNumFramesInFlight> deletion_queues_;
};

class Renderer::BindlessTexturePool final {
public:
    constexpr static size_t kNumTextureSets = kNumFramesInFlight;
    using TextureDescriptorHelper = DescriptorSetHelper<kNumTextureSets>;
    using TexturePool = ResourcePool<Texture, TextureTag, kNumTexturePoolSize>;

    static auto create(Renderer *renderer) -> std::unique_ptr<BindlessTexturePool> {
        std::unique_ptr<BindlessTexturePool> pool{new (std::nothrow) BindlessTexturePool()};
        if (!pool) {
            LogError("vulkan: renderer failed to allocate bindless texture pool");
            return nullptr;
        }

        pool->renderer_ = renderer;

        const TextureDescriptorHelper::Description texture_descriptor_desc = {
            .layout =
                {
                    // PerFrameTexturePool
                    TextureDescriptorHelper::DescriptorDescription{
                        .type = TextureDescriptorHelper::DescriptorDataType::eSamplerTexture,
                        .stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                        .num_bindings = kNumTexturePoolSize,
                    },
                },
        };

        pool->descriptor_helper_ = TextureDescriptorHelper::create(renderer, texture_descriptor_desc);

        // create a placeholder image for null textures
        {
            constexpr uint32_t kNullImageWidth = 4;
            constexpr uint32_t kNullImageHeight = 4;
            constexpr uint32_t kNullImageDepth = 4; // rgba

            std::vector<uint8_t> pixels;
            pixels.resize(kNullImageWidth * kNullImageWidth * kNullImageDepth);

            auto p = pixels.begin();
            for (uint32_t y = 0; y < kNullImageHeight; ++y) {
                for (uint32_t x = 0; x < kNullImageWidth; ++x) {
                    (*p++) = (x % 2) == (y % 2) ? 0 : 255;
                    (*p++) = 0;
                    (*p++) = (x % 2) == (y % 2) ? 0 : 255;
                    (*p++) = 255;
                }
            }

            pool->null_image_ = pool->renderer_->context_->memory().createImageRgba(
                VK_IMAGE_USAGE_SAMPLED_BIT, {kNullImageWidth, kNullImageHeight}, pixels);
            pool->null_image_view_ = pool->null_image_.createView(
                VK_IMAGE_VIEW_TYPE_2D, pool->null_image_.format(), VK_IMAGE_ASPECT_COLOR_BIT);

            VkSamplerCreateInfo sampler_desc = {
                .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                .magFilter = VK_FILTER_NEAREST,
                .minFilter = VK_FILTER_NEAREST,
                .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                .anisotropyEnable = VK_TRUE,
                .maxAnisotropy = 8.0f,
                .maxLod = 1,
            };

            VK_CHECK_ERROR(
                vkCreateSampler(pool->renderer_->context_->device(), &sampler_desc, nullptr, &pool->null_sampler_));

            for (auto &descriptor : pool->descriptors_) {
                descriptor.imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
                descriptor.imageView = pool->null_image_view_.view();
                descriptor.sampler = pool->null_sampler_;
            }
        }

        return pool;
    }

    ~BindlessTexturePool() {
        if (VK_NULL_HANDLE != null_sampler_) {
            vkDestroySampler(renderer_->context_->device(), null_sampler_, nullptr);
            null_sampler_ = VK_NULL_HANDLE;
        }
    }

    BindlessTexturePool(const BindlessTexturePool &) = delete;
    auto operator=(const BindlessTexturePool &) = delete;

    BindlessTexturePool(BindlessTexturePool &&) noexcept = delete;
    auto operator=(BindlessTexturePool &&) noexcept = delete;

    auto descriptorHelper() const -> const TextureDescriptorHelper & { return *descriptor_helper_; }
    auto descriptorSet(uint32_t frame) const -> VkDescriptorSet { return descriptor_helper_->getSetForFrame(frame); }
    auto descriptorSetLayout() const -> VkDescriptorSetLayout { return descriptor_helper_->layout(); }

    auto storeResource(Texture resource, uint32_t frame) -> std::optional<TexturePool::Id> {
        // get the handles before moving
        const auto vk_view = resource.imageView().view();
        const auto vk_sampler = resource.sampler();

        const auto id = pool_.storeResource(std::move(resource), frame);
        if (!id.has_value()) {
            return std::nullopt;
        }

        const auto index = id->index();
        descriptors_[index].sampler = vk_sampler;
        descriptors_[index].imageView = vk_view;
        descriptors_[index].imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;

        setAllBits(true);
        return id;
    }

    auto getResource(const TexturePool::Id &id) const -> const Texture * { return pool_.getResource(id); }

    auto refResource(const TexturePool::Id id, uint32_t frame) -> const Texture * {
        return pool_.refResource(id, frame);
    }

    auto destroyResource(const TexturePool::Id &id) -> void { return pool_.destroyResource(id); }

    auto garbageCollect(uint32_t last_frame) -> void {
        pool_.garbageCollect(last_frame, [&](uint32_t index, [[maybe_unused]] Texture &&t) {
            descriptors_[index].sampler = null_sampler_;
            descriptors_[index].imageView = null_image_view_.view();
            descriptors_[index].imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;

            setAllBits(true);
        });
    }

    auto updateDescriptorSet(uint32_t frame) -> void {
        if (!dirty_bit_[frame]) {
            return;
        }

        VkWriteDescriptorSet write_set_desc = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptorSet(frame),
            .dstBinding = 0,
            .descriptorCount = kNumTexturePoolSize,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = descriptors_.data(),
        };

        vkUpdateDescriptorSets(renderer_->context_->device(), 1, &write_set_desc, 0, nullptr);

        LogInfo("vulkan: renderer updated texture descriptor set for frame {}", frame);
        dirty_bit_[frame] = false;
    }

private:
    BindlessTexturePool() {
        for (auto &dirty_bit : dirty_bit_) {
            dirty_bit = false;
        }

        for (auto &descriptor : descriptors_) {
            descriptor.sampler = VK_NULL_HANDLE;
            descriptor.imageView = VK_NULL_HANDLE;
            descriptor.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        }
    }

    auto setAllBits(bool value) -> void { std::fill(dirty_bit_.begin(), dirty_bit_.end(), value); }

    Renderer *renderer_ = nullptr;
    std::array<bool, kNumTextureSets> dirty_bit_;

    Image null_image_;
    Image::View null_image_view_;
    VkSampler null_sampler_ = VK_NULL_HANDLE;

    std::unique_ptr<TextureDescriptorHelper> descriptor_helper_;
    ResourcePool<Texture, TextureTag, kNumTexturePoolSize> pool_;
    std::array<VkDescriptorImageInfo, kNumTexturePoolSize> descriptors_;
};

Renderer::BufferHelper::BufferHelper(Renderer *renderer, size_t size)
    : renderer_{renderer}, context_{renderer_->context_}, size_{size} {
    device_buffer_ = context_->memory().createDeviceBuffer(
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, size_);
    staging_buffer_ = context_->memory().createSharedBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, size_);

    VkBufferDeviceAddressInfo addr_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = device_buffer_.buffer(),
    };

    device_address_ = vkGetBufferDeviceAddress(context_->device(), &addr_info);
    ::memset(staging_buffer_.cpuMappedPointer(), 0, size_);
}

Renderer::BufferHelper::~BufferHelper() noexcept { device_address_ = 0ull; }

Renderer::BufferHelper::BufferHelper(BufferHelper &&b) noexcept {
    renderer_ = b.renderer_;
    context_ = b.context_;

    size_ = b.size_;

    staging_buffer_ = std::move(b.staging_buffer_);
    device_buffer_ = std::move(b.device_buffer_);
    device_address_ = std::move(b.device_address_);

    b.device_address_ = 0ull;
    b.size_ = 0ull;
}

auto Renderer::BufferHelper::operator=(BufferHelper &&b) noexcept -> BufferHelper & {
    if (this != &b) {
        renderer_ = b.renderer_;
        context_ = b.context_;

        size_ = b.size_;

        staging_buffer_ = std::move(b.staging_buffer_);
        device_buffer_ = std::move(b.device_buffer_);
        device_address_ = std::move(b.device_address_);

        b.device_address_ = 0ull;
        b.size_ = 0ull;
    }

    return *this;
}

auto Renderer::BufferHelper::upload(VkCommandBuffer command_buffer, std::span<const uint8_t> data) -> void {
    // copy to staging buffer
    ::memcpy(staging_buffer_.cpuMappedPointer(), data.data(), std::min(data.size_bytes(), size_));

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
        .size = size_,
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

Renderer::Mesh::Mesh(Mesh &&m) noexcept {
    renderer_ = std::move(m.renderer_);
    num_vertices_ = std::move(m.num_vertices_);
    num_indices_ = std::move(m.num_indices_);
    vertex_buffer_ = std::move(m.vertex_buffer_);
    index_bufer_ = std::move(m.index_bufer_);
    vertex_buffer_size_ = std::move(m.vertex_buffer_size_);
    index_buffer_size_ = std::move(m.index_buffer_size_);

    m.renderer_ = nullptr;
    m.num_vertices_ = 0;
    m.num_indices_ = 0;
    m.vertex_buffer_size_ = 0;
    m.index_buffer_size_ = 0;
}

auto Renderer::Mesh::operator=(Mesh &&m) noexcept -> Mesh & {
    if (this != &m) {
        renderer_ = std::move(m.renderer_);
        num_vertices_ = std::move(m.num_vertices_);
        num_indices_ = std::move(m.num_indices_);
        vertex_buffer_ = std::move(m.vertex_buffer_);
        index_bufer_ = std::move(m.index_bufer_);
        vertex_buffer_size_ = std::move(m.vertex_buffer_size_);
        index_buffer_size_ = std::move(m.index_buffer_size_);

        m.renderer_ = nullptr;
        m.num_vertices_ = 0;
        m.num_indices_ = 0;
        m.vertex_buffer_size_ = 0;
        m.index_buffer_size_ = 0;
    }

    return *this;
}

auto Renderer::Mesh::create(Renderer *renderer, const Description &desc) -> std::optional<Mesh> {
    Mesh mesh;

    mesh.renderer_ = renderer;
    const auto &memory = mesh.renderer_->context_->memory();

    const size_t aligned_buffer_size = util::bytes::align_ptr(desc.num_vertices, kVertexBufferAlign) * desc.vertex_size;
    assert(aligned_buffer_size > desc.vertex_buffer.size_bytes());

    mesh.num_vertices_ = desc.num_vertices;
    mesh.num_indices_ = std::size(desc.indices);

    mesh.vertex_buffer_size_ = aligned_buffer_size;
    mesh.index_buffer_size_ = desc.indices.size() * sizeof(uint32_t);

    mesh.vertex_buffer_ = memory.createBuffer(
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | desc.vertex_buffer_flags, desc.vertex_buffer, aligned_buffer_size);
    mesh.index_bufer_ = memory.createBuffer(
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | desc.index_buffer_flags,
        std::span<const uint8_t>{reinterpret_cast<const uint8_t *>(desc.indices.data()), mesh.index_buffer_size_});

    return std::move(mesh);
}

Renderer::Texture::Texture(Texture &&t) noexcept {
    renderer_ = std::move(t.renderer_);
    description_ = std::move(t.description_);
    image_ = std::move(t.image_);
    image_view_ = std::move(t.image_view_);
    sampler_ = std::move(t.sampler_);

    t.renderer_ = nullptr;
    t.sampler_ = VK_NULL_HANDLE;
}

auto Renderer::Texture::operator=(Texture &&t) noexcept -> Texture & {
    if (this != &t) {
        renderer_ = std::move(t.renderer_);
        description_ = std::move(t.description_);
        image_ = std::move(t.image_);
        image_view_ = std::move(t.image_view_);
        sampler_ = std::move(t.sampler_);

        t.renderer_ = nullptr;
        t.sampler_ = VK_NULL_HANDLE;
    }

    return *this;
}

auto Renderer::Texture::fromRgba(Renderer *renderer, const Description &desc, std::span<const uint8_t> rgba_data)
    -> std::optional<Texture> {
    Texture texture;

    texture.renderer_ = renderer;
    texture.description_ = desc;

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
        return std::nullopt;
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
        return std::nullopt;
    }

    texture.image_ = texture.renderer_->context_->memory().createImageRgba(
        VK_IMAGE_USAGE_SAMPLED_BIT, {desc.width, desc.height}, rgba_data);
    texture.image_view_ =
        texture.image().createView(VK_IMAGE_VIEW_TYPE_2D, texture.image().format(), VK_IMAGE_ASPECT_COLOR_BIT);

    VkSamplerCreateInfo sampler_desc = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = mag_filter,
        .minFilter = min_filter,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .anisotropyEnable = VK_TRUE,
        .maxAnisotropy = 8.0f,
        .maxLod = 1,
    };

    VK_CHECK_ERROR(vkCreateSampler(texture.renderer_->context_->device(), &sampler_desc, nullptr, &texture.sampler_));
    return std::move(texture);
}

Renderer::Texture::~Texture() noexcept {
    if (VK_NULL_HANDLE != sampler_) {
        vkDestroySampler(renderer_->context_->device(), sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }
}

auto Renderer::ActorMesh::create(Renderer *renderer, AnimatedMeshId mesh) -> std::optional<ActorMesh> {
    const auto *base_mesh = renderer->getAnimMesh(mesh);
    if (!base_mesh) {
        return std::nullopt;
    }

    auto bone_buffer = SharedDataBuffer<cbSkinningBuffer>::create(renderer);
    if (!bone_buffer.has_value()) {
        return std::nullopt;
    }

    ActorMesh actor{renderer, mesh, std::move(bone_buffer.value())};
    actor.num_vertices_ = util::bytes::align_ptr(base_mesh->numVertices(), kVertexBufferAlign);
    actor.output_buffer_size_ = actor.num_vertices_ * sizeof(StaticVertex);

    for (uint32_t i = 0; i < kNumFramesInFlight; ++i) {
        actor.output_buffer_[i] = actor.renderer_->context_->memory().createDeviceBuffer(
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            actor.output_buffer_size_);
    }

    return actor;
}

auto Renderer::create(const Description &description) -> std::unique_ptr<Renderer> {
    std::unique_ptr<Renderer> renderer{new (std::nothrow) Renderer()};
    if (!renderer) {
        LogError("vulkan: cannot allocate renderer object");
        return {};
    }

    renderer->context_ = description.context;
    renderer->texture_pool_ = BindlessTexturePool::create(renderer.get());
    renderer->mesh_pool_ = std::make_unique<ResourcePool<Mesh, MeshTag, kNumMeshPoolSize>>();
    renderer->anim_mesh_pool_ = std::make_unique<ResourcePool<Mesh, AnimatedMeshTag, kNumAnimMeshPoolSize>>();
    renderer->actor_mesh_pool_ = std::make_unique<ResourcePool<ActorMesh, ActorMeshTag, kNumMaxSkinnedObjects>>();
    renderer->createSwapchainData();

    for (size_t i = 0; i < kNumFramesInFlight; ++i) {
        renderer->frames_[i].scene_buffer = TypedBufferHelper<cbFrameHeapBuffer>::create(renderer.get());
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

    renderer->skinning_pass_ = ComputeSkinningPass::create(renderer.get(), description.shader_loader.get());
    if (!renderer->skinning_pass_) {
        LogError("vulkan: renderer cannot initialize compute skinning pipeline");
        return nullptr;
    }

    renderer->geometry_pass_ = OpaqueGeometryPass::create(renderer.get(), description.shader_loader.get());
    if (!renderer->geometry_pass_) {
        LogError("vulkan: renderer cannot initialize graphics pipeline");
        return nullptr;
    }

    return renderer;
}

auto Renderer::addMesh(Mesh &&mesh) -> std::optional<MeshId> {
    return mesh_pool_->storeResource(std::move(mesh), current_frame_);
}

auto Renderer::addTexture(Texture &&texture) -> std::optional<TextureId> {
    return texture_pool_->storeResource(std::move(texture), current_frame_);
}

auto Renderer::addAnimMesh(Mesh &&mesh) -> std::optional<AnimatedMeshId> {
    return anim_mesh_pool_->storeResource(std::move(mesh), current_frame_);
}

auto Renderer::addActorMesh(ActorMesh &&mesh) -> std::optional<ActorMeshId> {
    return actor_mesh_pool_->storeResource(std::move(mesh), current_frame_);
}

auto Renderer::getMesh(const MeshId &id) const -> const Mesh * { return mesh_pool_->getResource(id); }
auto Renderer::getTexture(const TextureId &id) const -> const Texture * { return texture_pool_->getResource(id); }
auto Renderer::getAnimMesh(const AnimatedMeshId &id) const -> const Mesh * { return anim_mesh_pool_->getResource(id); }

auto Renderer::getActorMesh(const ActorMeshId &id) const -> const ActorMesh * {
    return actor_mesh_pool_->getResource(id);
}

auto Renderer::getActorMesh(const ActorMeshId &id) -> ActorMesh * { return actor_mesh_pool_->getResource(id); }

auto Renderer::unrefMesh(const MeshId &id) -> void { mesh_pool_->destroyResource(id); }
auto Renderer::unrefTexture(const TextureId &id) -> void { texture_pool_->destroyResource(id); }
auto Renderer::unrefAnimMesh(const AnimatedMeshId &id) -> void { anim_mesh_pool_->destroyResource(id); }
auto Renderer::unrefActorMesh(const ActorMeshId &id) -> void { actor_mesh_pool_->destroyResource(id); }

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

auto Renderer::ComputeSkinningPass::create(Renderer *renderer, const IShaderLoader *shader_loader)
    -> std::unique_ptr<ComputeSkinningPass> {
    std::unique_ptr<ComputeSkinningPass> pass{new (std::nothrow) ComputeSkinningPass()};
    if (!pass) {
        LogError("vulkan: renderer cannot allocate compute skinning pass resources");
        return nullptr;
    }

    pass->renderer_ = renderer;

    const auto device = pass->renderer_->context_->device();

    const auto shader_buffer = shader_loader->loadSkinningPassShader();
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
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .size = sizeof(cbPushConstantBuffer),
    };

    VkPipelineLayoutCreateInfo pipeline_layout_desc = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 0,
        .pSetLayouts = nullptr,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constant_range,
    };

    VK_CHECK_ERROR(vkCreatePipelineLayout(device, &pipeline_layout_desc, nullptr, &pass->pipeline_layout_));

    VkComputePipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage =
            VkPipelineShaderStageCreateInfo{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = pass->shader_module_,
                .pName = "main",
            },
        .layout = pass->pipeline_layout_,
    };

    VK_CHECK_ERROR(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pass->pipeline_));
    return pass;
}

Renderer::ComputeSkinningPass::~ComputeSkinningPass() noexcept {
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

    VkDescriptorSetLayout descriptor_layout = pass->renderer_->texture_pool_->descriptorSetLayout();

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

    // this frame has definitely ended rendering and all the resources can be garbage collected
    mesh_pool_->garbageCollect(current_frame_);
    texture_pool_->garbageCollect(current_frame_);

    // this frame is not executing, so we can touch its descriptor set
    texture_pool_->updateDescriptorSet(current_frame_);

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

    struct IndexedDrawCall {
        VkBuffer vertex_buffer = VK_NULL_HANDLE;
        VkBuffer index_buffer = VK_NULL_HANDLE;
        uint32_t num_indices = 0;
    };

    std::array<IndexedDrawCall, kNumMaxStaticObjects> indexed_draws = {};
    uint32_t num_indexed_draws = 0;

    // update frame data
    auto &scene_buffer = current_frame.scene_buffer;
    auto &scene_buffer_data = scene_buffer->storage();

    scene_buffer_data.projection = camera_.projection();
    scene_buffer_data.projection_inv = camera_.projectionInv();
    scene_buffer_data.view = camera_.view();
    scene_buffer_data.view_inv = camera_.viewInv();

    for (uint32_t i = 0; i < draw_queue_fill_ && num_indexed_draws < kNumMaxStaticObjects; ++i, ++num_indexed_draws) {
        const auto object_id = num_indexed_draws;
        scene_buffer_data.static_objects[object_id].world = draw_queue_[i]->world_matrix;

        if (draw_queue_[i]->diffuse_map.has_value()) {
            if (nullptr != texture_pool_->refResource(draw_queue_[i]->diffuse_map.value(), current_frame_)) {
                scene_buffer_data.static_objects[object_id].diffuse_map =
                    static_cast<int32_t>(draw_queue_[i]->diffuse_map.value().index());
            }
        } else {
            scene_buffer_data.static_objects[object_id].diffuse_map = -1;
        }

        if (draw_queue_[i]->normal_map.has_value()) {
            if (nullptr != texture_pool_->refResource(draw_queue_[i]->normal_map.value(), current_frame_)) {
                scene_buffer_data.static_objects[object_id].normal_map =
                    static_cast<int32_t>(draw_queue_[i]->normal_map.value().index());
            }
        } else {
            scene_buffer_data.static_objects[object_id].normal_map = -1;
        }

        auto &draw_call = indexed_draws[object_id];
        const auto *mesh_ref = mesh_pool_->refResource(draw_queue_[i]->mesh, current_frame_);

        draw_call.vertex_buffer = mesh_ref->vertexBuffer().buffer();
        draw_call.index_buffer = mesh_ref->indexBuffer().buffer();
        draw_call.num_indices = mesh_ref->numIndices();
    }

    // upload all queued compute skinning bone buffers
    for (uint32_t i = 0; i < skinning_queue_fill_ && num_indexed_draws < kNumMaxStaticObjects;
         ++i, ++num_indexed_draws) {
        const auto object_id = num_indexed_draws;
        scene_buffer_data.static_objects[object_id].world = skinning_queue_[i]->world_matrix;

        if (skinning_queue_[i]->diffuse_map.has_value()) {
            if (nullptr != texture_pool_->refResource(skinning_queue_[i]->diffuse_map.value(), current_frame_)) {
                scene_buffer_data.static_objects[object_id].diffuse_map =
                    static_cast<int32_t>(skinning_queue_[i]->diffuse_map.value().index());
            }
        } else {
            scene_buffer_data.static_objects[object_id].diffuse_map = -1;
        }

        if (skinning_queue_[i]->normal_map.has_value()) {
            if (nullptr != texture_pool_->refResource(skinning_queue_[i]->normal_map.value(), current_frame_)) {
                scene_buffer_data.static_objects[object_id].normal_map =
                    static_cast<int32_t>(skinning_queue_[i]->normal_map.value().index());
            }
        } else {
            scene_buffer_data.static_objects[object_id].normal_map = -1;
        }

        const auto actor_id = skinning_queue_[i]->skinned_mesh;
        auto *actor_ref = actor_mesh_pool_->refResource(actor_id, current_frame_);
        auto *mesh_ref = anim_mesh_pool_->refResource(actor_ref->inputMesh(), current_frame_);

        if (!actor_ref) {
            continue;
        }

        actor_ref->transformBuffer().upload(current_frame_);

        auto &draw_call = indexed_draws[object_id];

        draw_call.vertex_buffer = actor_ref->vertexBuffer(current_frame_).buffer();
        draw_call.index_buffer = mesh_ref->indexBuffer().buffer();
        draw_call.num_indices = mesh_ref->numIndices();
    }

    auto command_buffer = current_frame.command_buffer;
    VK_CHECK_ERROR(vkResetCommandBuffer(command_buffer, 0));

    VkCommandBufferBeginInfo command_buffer_begin_desc = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    VK_CHECK_ERROR(vkBeginCommandBuffer(command_buffer, &command_buffer_begin_desc));

    // invoke compute shader skinning
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, skinning_pass_->pipeline());

    ComputeSkinningPass::cbPushConstantBuffer skinning_constants;

    for (uint32_t i = 0; i < skinning_queue_fill_; ++i) {
        const auto actor_id = skinning_queue_[i]->skinned_mesh;
        auto *actor = actor_mesh_pool_->getResource(actor_id);
        auto *input_mesh = anim_mesh_pool_->getResource(actor->inputMesh());

        // pass the buffers using push constants
        skinning_constants.input_buffer = input_mesh->vertexBuffer().deviceAddress();
        skinning_constants.output_buffer = actor->vertexBuffer(current_frame_).deviceAddress();
        skinning_constants.bone_buffer = actor->transformBuffer().deviceAddress(current_frame_);

        const auto num_dispatches = input_mesh->num_vertices_ / kVertexBufferAlign;

        vkCmdPushConstants(
            command_buffer, skinning_pass_->pipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
            sizeof(ComputeSkinningPass::cbPushConstantBuffer), &skinning_constants);
        vkCmdDispatch(command_buffer, num_dispatches, 1, 1);
    }

    // upload buffer data - this also inserts a memory barrier!!
    scene_buffer->upload(command_buffer);

    VkMemoryBarrier2 compute_mem_barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT,
        .dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT,
    };

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

    VkDependencyInfo render_dependency_desc = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &compute_mem_barrier,
        .imageMemoryBarrierCount = static_cast<uint32_t>(output_barriers.size()),
        .pImageMemoryBarriers = output_barriers.data(),
    };

    vkCmdPipelineBarrier2(command_buffer, &render_dependency_desc);

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

    VkDescriptorSet ds = texture_pool_->descriptorSet(current_frame_);
    vkCmdBindDescriptorSets(
        command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, geometry_pass_->pipelineLayout(), 0, 1, &ds, 0, nullptr);

    VkDeviceSize vertex_offset = 0;
    OpaqueGeometryPass::cbPushConstantBuffer push_constants;

    for (uint32_t i = 0; i < num_indexed_draws; ++i) {
        const auto &draw_call = indexed_draws[i];

        vkCmdBindVertexBuffers(command_buffer, 0, 1, &draw_call.vertex_buffer, &vertex_offset);
        vkCmdBindIndexBuffer(command_buffer, draw_call.index_buffer, 0, VK_INDEX_TYPE_UINT32);

        push_constants.frame_heap = scene_buffer->deviceAddress();
        push_constants.object_id = i;

        vkCmdPushConstants(
            command_buffer, geometry_pass_->pipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(OpaqueGeometryPass::cbPushConstantBuffer), &push_constants);

        vkCmdDrawIndexed(command_buffer, draw_call.num_indices, 1, 0, 0, 0);
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

    skinning_queue_fill_ = 0;
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
