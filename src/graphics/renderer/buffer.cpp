#include "graphics/renderer/buffer.hpp"

namespace graphics {

auto RendererBuffer::create(IResourceScheduler *scheduler, const Description &description)
    -> util::RefCountedPtr<RendererBuffer> {}

} // namespace graphics
