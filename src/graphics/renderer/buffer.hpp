#pragma once
#include <optional>
#include <span>

#include "common/refcounted.hpp"

#include "graphics/memory.hpp"
#include "graphics/renderer/resource.hpp"

namespace graphics {

class RendererBuffer final : public RendererResource {
public:
    enum Usage : VkBufferUsageFlags {
        eNone = 0,
        eTransferSrc = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        eTransferDst = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        eUniformTexelBuffer = VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
        eStorageTexelBuffer = VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
        eUniformBuffer = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        eStorageBuffer = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        eIndexBuffer = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        eVertexBuffer = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        eIndirectBuffer = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
        eBufferDeviceAddress = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
    };

    enum class MemoryType {
        eShared,
        eDevice,
    };

    struct Description {
        Usage usage;
        MemoryType memory;
        VkDeviceSize size;
    };

    static auto create(
        IResourceScheduler *scheduler, const Description &description,
        std::optional<std::span<const uint8_t>> init_data = std::nullopt) -> util::RefCountedPtr<RendererBuffer>;

    ~RendererBuffer() noexcept = default;

    RendererBuffer(const RendererBuffer &) = delete;
    auto operator=(const RendererBuffer &) = delete;

    RendererBuffer(RendererBuffer &&) = delete;
    auto operator=(RendererBuffer &&) = delete;

    auto description() const -> const Description & { return description_; }

    auto buffer() const -> const Buffer & { return buffer_; }
    auto addrOf() const -> VkBuffer const * { return buffer_.addrOf(); }
    auto nativeBuffer() const -> VkBuffer { return buffer_.buffer(); }
    auto deviceAddress() const -> VkDeviceAddress { return buffer_.deviceAddress(); }
    auto cpuMappedAddress() const -> void * { return buffer_.cpuMappedPointer(); }

private:
    RendererBuffer(IResourceScheduler *scheduler, Buffer &&buffer, Description description)
        : RendererResource{scheduler}, buffer_{std::move(buffer)}, description_{description} {}

    Buffer buffer_ = {};
    Description description_ = {};
};

auto operator|(const RendererBuffer::Usage &u1, const RendererBuffer::Usage &u2) -> RendererBuffer::Usage;
auto operator&(const RendererBuffer::Usage &u1, const RendererBuffer::Usage &u2) -> RendererBuffer::Usage;
auto operator~(const RendererBuffer::Usage &u) -> RendererBuffer::Usage;

} // namespace graphics
