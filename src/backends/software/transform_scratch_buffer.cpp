#include <chronon3d/backends/software/transform_scratch_buffer.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/math/color.hpp>

namespace chronon3d {

TransformScratchBuffer::TransformScratchBuffer(TransformScratchBuffer&& other) noexcept
    : m_storage(other.m_storage)
    , m_owner(std::move(other.m_owner))
{
    // Leave the source in a valid empty state so its destructor is a no-op.
    other.m_storage = nullptr;
    // other.m_owner is now null after the move.
}

TransformScratchBuffer& TransformScratchBuffer::operator=(TransformScratchBuffer&& other) noexcept {
    if (this != &other) {
        // Release our current storage first.
        m_storage = nullptr;
        m_owner.reset();
        // Steal from the source.
        m_storage = other.m_storage;
        m_owner = std::move(other.m_owner);
        other.m_storage = nullptr;
    }
    return *this;
}

void TransformScratchBuffer::ensure_capacity(int width, int height) {
    if (m_storage && m_storage->allocated_width() >= width && m_storage->allocated_height() >= height) {
        return; // already large enough
    }
    const auto [bw, bh] = cache::FramebufferPool::round_to_bucket(width, height);
    m_owner = std::make_unique<Framebuffer>(bw, bh);
    m_owner->clear(Color::transparent());
    m_storage = m_owner.get();
}

Framebuffer* TransformScratchBuffer::acquire() {
    if (m_storage) {
        m_storage->clear(Color::transparent());
    }
    return m_storage;
}

TransformScratchBuffer::Handle TransformScratchBuffer::acquire_handle() {
    Framebuffer* fb = acquire();
    return Handle(this, fb);
}

void TransformScratchBuffer::reset() {
    m_storage = nullptr;
    m_owner.reset();
}

Framebuffer* TransformScratchBuffer::Handle::detach() {
    Framebuffer* fb = m_fb;
    m_owner = nullptr;
    m_fb = nullptr;
    return fb;
}

void TransformScratchBuffer::Handle::release() {
    if (m_owner && m_fb) {
        // Restore the scratch pointer to the owner so the next frame can
        // reuse the buffer.  This mirrors the old scratch_slot restoration
        // done by the PoolFbDeleter.
        // Note: we do NOT clear() here — the caller is expected to leave
        // the buffer in a sensible state, and the next ensure_capacity()
        // or acquire() will clear if needed.
        if (m_owner->m_storage == nullptr) {
            m_owner->m_storage = m_fb;
        }
    }
    m_owner = nullptr;
    m_fb = nullptr;
}

} // namespace chronon3d
