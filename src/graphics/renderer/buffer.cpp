#include "logger.hpp"
#include "graphics/renderer/buffer.hpp"

namespace graphics {

auto RendererBuffer::create(
    IResourceScheduler *scheduler, const Description &desc, std::optional<std::span<const uint8_t>> init_data)
    -> util::RefCountedPtr<RendererBuffer> {

    Description description = desc;

    if (init_data.has_value() && description.size == 0) {
        description.size = init_data->size_bytes();
    } else if (init_data.has_value() && description.size < init_data->size_bytes()) {
        LogError("vulkan: invalid buffer desc, init_data size is larger than allocated memory size");
        return nullptr;
    }

    if (description.size == 0) {
        LogError("vulkan: invalid buffer desc, memory size is zero");
        return nullptr;
    }

    const auto usage = static_cast<VkBufferUsageFlags>(description.usage);
    Buffer native_buffer = {};

    switch (description.memory) {
    case graphics::RendererBuffer::MemoryType::eDevice:
        native_buffer = scheduler->context()->memory().createSharedBuffer(usage, description.size);
        if (init_data.has_value()) {
            std::memcpy(native_buffer.cpuMappedPointer(), init_data->data(), init_data->size_bytes());
        }

        break;

    case graphics::RendererBuffer::MemoryType::eHost:
        if (init_data.has_value()) {
            native_buffer = scheduler->context()->memory().createBuffer(usage, init_data.value());
        } else {
            native_buffer = scheduler->context()->memory().createDeviceBuffer(usage, description.size);
        }

        break;

    default:
        LogError("vulkan: cannot create buffer in invalid memory");
        return nullptr;
    }

    return util::RefCountedPtr<RendererBuffer>{
        new RendererBuffer(scheduler, std::move(native_buffer), std::move(description))};
}

auto operator|(const RendererBuffer::Usage &u1, const RendererBuffer::Usage &u2) -> RendererBuffer::Usage {
    return static_cast<RendererBuffer::Usage>(static_cast<uint32_t>(u1) | static_cast<uint32_t>(u2));
}

auto operator&(const RendererBuffer::Usage &u1, const RendererBuffer::Usage &u2) -> RendererBuffer::Usage {
    return static_cast<RendererBuffer::Usage>(static_cast<uint32_t>(u1) & static_cast<uint32_t>(u2));
}

auto operator~(const RendererBuffer::Usage &u) -> RendererBuffer::Usage {
    return static_cast<RendererBuffer::Usage>(~static_cast<uint32_t>(u));
}

} // namespace graphics
