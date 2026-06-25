#include <chronon3d/backends/software/scratch_buffer.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/math/color.hpp>
#include <memory>

namespace chronon3d {

Framebuffer* TransformScratchBuffer::ensure_size(int width, int height) {
    // Reuse the existing scratch when its allocated bucket already covers
    // the requested size — preserves the large rounded bucket between
    // frames so the pool isn't churned by a few-pixel animated-transform
    // size deltas.  Ownership stays in `m_scratch_owner` (RAII); the
    // returned raw pointer aliases `m_scratch_owner.get()` (i.e.
    // `m_scratch_ptr`) for callers that need a `Framebuffer*`.
    if (m_scratch_owner &&
        m_scratch_owner->allocated_width() >= width &&
        m_scratch_owner->allocated_height() >= height) {
        return m_scratch_ptr;
    }
    const auto [bw, bh] = cache::FramebufferPool::round_to_bucket(width, height);
    m_scratch_owner = std::make_unique<Framebuffer>(bw, bh);
    m_scratch_ptr = m_scratch_owner.get();
    m_scratch_owner->clear(Color::transparent());
    return m_scratch_ptr;
}

void TransformScratchBuffer::reset() {
    m_scratch_owner.reset();
    m_scratch_ptr = nullptr;
}

TransformScratchBuffer::TransformScratchBuffer(TransformScratchBuffer&& other) noexcept
    : m_scratch_owner(std::move(other.m_scratch_owner))
    , m_scratch_ptr(other.m_scratch_ptr)
{
    other.m_scratch_ptr = nullptr;
}

TransformScratchBuffer& TransformScratchBuffer::operator=(TransformScratchBuffer&& other) noexcept {
    if (this != &other) {
        reset();
        m_scratch_owner = std::move(other.m_scratch_owner);
        m_scratch_ptr = other.m_scratch_ptr;
        other.m_scratch_ptr = nullptr;
    }
    return *this;
}

} // namespace chronon3d
