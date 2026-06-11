#include <chronon3d/backends/software/scratch_buffer.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/math/color.hpp>
#include <memory>

namespace chronon3d {

Framebuffer* TransformScratchBuffer::ensure_size(int width, int height) {
    if (m_scratch &&
        m_scratch->allocated_width() >= width &&
        m_scratch->allocated_height() >= height) {
        return m_scratch;
    }
    const auto [bw, bh] = cache::FramebufferPool::round_to_bucket(width, height);
    delete m_scratch;
    m_scratch = std::make_unique<Framebuffer>(bw, bh).release();
    m_scratch->clear(Color::transparent());
    return m_scratch;
}

void TransformScratchBuffer::reset() {
    delete m_scratch;
    m_scratch = nullptr;
}

TransformScratchBuffer::TransformScratchBuffer(TransformScratchBuffer&& other) noexcept
    : m_scratch(other.m_scratch)
{
    other.m_scratch = nullptr;
}

TransformScratchBuffer& TransformScratchBuffer::operator=(TransformScratchBuffer&& other) noexcept {
    if (this != &other) {
        reset();
        m_scratch = other.m_scratch;
        other.m_scratch = nullptr;
    }
    return *this;
}

} // namespace chronon3d
