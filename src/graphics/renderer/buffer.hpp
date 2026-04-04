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
        eTransferSrc = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        eTransferDst = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        eUniformTexelBuffer = VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
        eStorageTexelBuffer = VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
        eIndexBuffer = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        eVertexBuffer = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        eIndirectBuffer = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
        eBufferDeviceAddress = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
    };

    enum class MemoryType {
        eHost,
        eDevice,
    };

    struct Description {
        VkDeviceSize size;
        Usage usage;
        MemoryType memory;
        std::optional<std::span<uint8_t>> init_data;
    };

    static auto create(IResourceScheduler *scheduler, const Description &description)
        -> util::RefCountedPtr<RendererBuffer>;

    ~RendererBuffer() noexcept = default;

    RendererBuffer(const RendererBuffer &) = delete;
    auto operator=(const RendererBuffer &) = delete;

    RendererBuffer(RendererBuffer &&) = delete;
    auto operator=(RendererBuffer &&) = delete;

    auto description() const -> const Description & { return description_; }

    auto buffer() const -> const Buffer & { return buffer_; }
    auto nativeBuffer() const -> VkBuffer { return buffer_.buffer(); }

private:
    RendererBuffer(IResourceScheduler *scheduler, Buffer &&buffer, Description description);

    Context *context_ = nullptr;
    Buffer buffer_ = {};
    Description description_ = {};
};

} // namespace graphics
