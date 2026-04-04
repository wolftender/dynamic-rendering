#pragma once
#include <optional>
#include <span>

#include "graphics/memory.hpp"
#include "graphics/renderer/resource.hpp"
#include "graphics/renderer/scheduler.hpp"

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

    static auto create(RendererScheduler *scheduler, const Description &description)
        -> util::RefCountedPtr<RendererBuffer>;
};

} // namespace graphics
