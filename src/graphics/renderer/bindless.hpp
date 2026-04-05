#pragma once
#include <array>
#include <bitset>
#include <cstdint>

#include "common/refcounted.hpp"
#include "common/managedpool.hpp"

#include "graphics/renderer/descriptor.hpp"
#include "graphics/renderer/texture.hpp"
#include "graphics/renderer/scheduler.hpp"

namespace graphics {

template <uint32_t kNumTexturePoolSize> class BindlessTexturePool final {
public:
    struct TextureTag {};

    using TextureDescriptorArray = RendererScheduler::DescriptorSetArray;
    using TexturePool = util::ManagedPool<util::RefCountedPtr<RendererTexture>, kNumTexturePoolSize, TextureTag>;
    using Id = TexturePool::Id;

    static auto create(RendererScheduler *scheduler) -> std::unique_ptr<BindlessTexturePool> {
        std::unique_ptr<BindlessTexturePool> pool{new (std::nothrow) BindlessTexturePool()};
        if (!pool) {
            LogError("vulkan: renderer failed to allocate bindless texture pool");
            return nullptr;
        }

        pool->scheduler_ = scheduler;

        const DescriptorLayout::Description texture_descriptor_desc = {
            .layout =
                {
                    // PerFrameTexturePool
                    DescriptorDescription{
                        .type = DescriptorDataType::eSamplerTexture,
                        .stages = DescriptorShaderStage::eVertex | DescriptorShaderStage::eFragment,
                        .num_bindings = kNumTexturePoolSize,
                    },
                },
        };

        pool->descriptor_helper_ = TextureDescriptorArray::create(scheduler, texture_descriptor_desc);

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

            RendererTexture::RgbaDescription rgba_description = {
                .width = kNullImageWidth,
                .height = kNullImageHeight,
                .min_filter = RendererTexture::MinFilter::eNearest,
                .mag_filter = RendererTexture::MagFilter::eNearest,
                .usage = RendererTexture::Usage::eUsageShaderSample,
                .init_data = pixels,
            };

            pool->null_texture_ = RendererTexture::createFromRgba(scheduler, rgba_description);

            for (auto &descriptor : pool->descriptors_) {
                descriptor.imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
                descriptor.imageView = pool->null_texture_->nativeView();
                descriptor.sampler = pool->null_texture_->nativeSampler();
            }
        }

        return pool;
    }

    ~BindlessTexturePool() = default;

    BindlessTexturePool(const BindlessTexturePool &) = delete;
    auto operator=(const BindlessTexturePool &) = delete;

    BindlessTexturePool(BindlessTexturePool &&) noexcept = delete;
    auto operator=(BindlessTexturePool &&) noexcept = delete;

    auto descriptorArray() const -> const TextureDescriptorArray & { return *descriptor_helper_; }
    auto descriptorLayout() const -> DescriptorLayout * { return descriptor_helper_->layout(); }

    auto descriptorSet() const -> VkDescriptorSet {
        return descriptor_helper_->getSetForFrame(scheduler_->currentFrameIndex());
    }

    auto storeResource(util::RefCountedPtr<RendererTexture> resource) -> std::optional<Id> {
        // get the handles before moving
        const auto vk_view = resource->nativeView();
        const auto vk_sampler = resource->nativeSampler();

        const auto id = pool_.store(std::move(resource));
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

    auto getResource(const TexturePool::Id &id) const -> const util::RefCountedPtr<RendererTexture> {
        return pool_.getResource(id);
    }

    auto destroyResource(const TexturePool::Id &id) -> void {
        const auto index = id.index();

        descriptors_[index].sampler = null_texture_->nativeSampler();
        descriptors_[index].imageView = null_texture_->nativeView();

        return pool_.destroyResource(id);
    }

    auto updateDescriptorSet() -> void {
        const auto frame = scheduler_->currentFrameIndex();

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

        vkUpdateDescriptorSets(scheduler_->context()->device(), 1, &write_set_desc, 0, nullptr);

        LogInfo("vulkan: renderer updated texture descriptor set for frame {}", frame);
        dirty_bit_[frame] = false;
    }

private:
    BindlessTexturePool() {
        setAllBits(true);

        for (auto &descriptor : descriptors_) {
            descriptor.sampler = VK_NULL_HANDLE;
            descriptor.imageView = VK_NULL_HANDLE;
            descriptor.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        }
    }

    auto setAllBits(bool value) -> void {
        if (value) {
            dirty_bit_.set();
        } else {
            dirty_bit_.reset();
        }
    }

    RendererScheduler *scheduler_ = nullptr;
    std::bitset<RendererScheduler::kNumFramesInFlight> dirty_bit_;

    util::RefCountedPtr<RendererTexture> null_texture_;
    std::unique_ptr<TextureDescriptorArray> descriptor_helper_;
    std::array<VkDescriptorImageInfo, kNumTexturePoolSize> descriptors_;

    TexturePool pool_;
};

} // namespace graphics
