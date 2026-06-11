#include <chronon3d/backends/software/buffer_ring.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/math/color.hpp>

namespace chronon3d {

void RendererBufferRing::ensure_size(int width, int height) {
    auto allocate_ping = [&](int idx) {
        if (m_ping_fb[idx] &&
            m_ping_fb[idx]->allocated_width() >= width &&
            m_ping_fb[idx]->allocated_height() >= height) {
            return; // already large enough
        }
        // If prev_framebuffer points to this ping, reset before deleting
        // to avoid dangling pointer on resolution change.
        if (m_ping_fb[idx] && m_prev_framebuffer.get() == m_ping_fb[idx]) {
            m_prev_framebuffer.reset();
        }
        delete m_ping_fb[idx];
        auto [bw, bh] = cache::FramebufferPool::round_to_bucket(width, height);
        m_ping_fb[idx] = new Framebuffer(bw, bh);
        m_ping_fb[idx]->resize_logical(width, height);
        m_ping_fb[idx]->set_origin(0, 0);
        m_ping_fb[idx]->clear(Color::transparent());
    };
    allocate_ping(0);
    allocate_ping(1);
    // NOTE: do NOT reset m_ping_read_idx / m_ping_write_idx here.
    // The indices are initialized by the constructor (read=0, write=1)
    // and updated by swap() after each frame. Resetting them here would
    // break the alternating ping-pong cycle.
    // CRITICAL: do NOT touch m_prev_framebuffer here. It must continue to
    // point to the actual previous frame's content. swap() is the only
    // place that should repoint it.
}

void RendererBufferRing::swap() {
    std::swap(m_ping_read_idx, m_ping_write_idx);
    // Re-point prev_framebuffer to the new read ping (ex-write, now
    // the completed frame that the writer and early-exit paths see).
    // Use a raw pointer wrapped in shared_ptr with no-op deleter —
    // the ring owns the memory and manages its lifetime.
    m_prev_framebuffer = std::shared_ptr<Framebuffer>(
        m_ping_fb[m_ping_read_idx],
        [](Framebuffer*) noexcept {});
}

Framebuffer* RendererBufferRing::ping_fb(int idx) const noexcept {
    if (idx < 0 || idx > 1) return nullptr;
    return m_ping_fb[idx];
}

void RendererBufferRing::reset() {
    m_prev_framebuffer.reset();
    delete m_ping_fb[0];
    m_ping_fb[0] = nullptr;
    delete m_ping_fb[1];
    m_ping_fb[1] = nullptr;
    m_ping_read_idx = 0;
    m_ping_write_idx = 1;
}

RendererBufferRing::RendererBufferRing(RendererBufferRing&& other) noexcept
    : m_ping_read_idx(other.m_ping_read_idx)
    , m_ping_write_idx(other.m_ping_write_idx)
    , m_prev_framebuffer(std::move(other.m_prev_framebuffer))
{
    m_ping_fb[0] = other.m_ping_fb[0];
    m_ping_fb[1] = other.m_ping_fb[1];
    other.m_ping_fb[0] = nullptr;
    other.m_ping_fb[1] = nullptr;
}

RendererBufferRing& RendererBufferRing::operator=(RendererBufferRing&& other) noexcept {
    if (this != &other) {
        reset();
        m_ping_fb[0] = other.m_ping_fb[0];
        m_ping_fb[1] = other.m_ping_fb[1];
        other.m_ping_fb[0] = nullptr;
        other.m_ping_fb[1] = nullptr;
        m_ping_read_idx = other.m_ping_read_idx;
        m_ping_write_idx = other.m_ping_write_idx;
        m_prev_framebuffer = std::move(other.m_prev_framebuffer);
    }
    return *this;
}

} // namespace chronon3d
