#include <chronon3d/backends/software/renderer_buffer_ring.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/math/color.hpp>

namespace chronon3d {

namespace {

// Helper: create a fresh ping buffer at the requested (logical) size,
// rounding to bucket for pool-friendly allocations.
std::unique_ptr<Framebuffer> make_ping(int width, int height) {
    auto [bw, bh] = cache::FramebufferPool::round_to_bucket(width, height);
    auto fb = std::make_unique<Framebuffer>(bw, bh);
    fb->resize_logical(width, height);
    fb->set_origin(0, 0);
    fb->clear(Color::transparent());
    return fb;
}

} // namespace

void RendererBufferRing::ensure_capacity(int width, int height) {
    auto realloc = [&](int idx) {
        auto& p = m_ping[idx];
        if (p && p->allocated_width() >= width && p->allocated_height() >= height) {
            return; // already large enough
        }
        p = make_ping(width, height);
    };
    realloc(0);
    realloc(1);
    // Indices are preserved across reallocation — swap state survives
    // resolution changes (a "frame" is just two buffers, we just resized
    // both).  We do NOT reset m_has_committed; the new pings are fresh,
    // and the new "previous frame" will be established by the next
    // commit_written_frame() call.
}

Framebuffer* RendererBufferRing::acquire_write() {
    Framebuffer* p = m_ping[m_write_idx].get();
    if (p) {
        p->clear(Color::transparent());
    }
    return p;
}

void RendererBufferRing::commit_written_frame() {
    std::swap(m_read_idx, m_write_idx);
    m_has_committed = true;
}

std::shared_ptr<Framebuffer> RendererBufferRing::previous_frame() const {
    if (!m_has_committed) {
        return nullptr;
    }
    Framebuffer* read = m_ping[m_read_idx].get();
    if (!read) {
        return nullptr;
    }
    // Build a non-owning shared_ptr view of the read ping.
    //
    // Approach: allocate a shared_ptr that takes ownership of a *new*
    // dummy Framebuffer-sized stub... no, that would not point at the
    // right memory.  Instead we use the aliasing constructor: pass an
    // empty shared_ptr (with a dummy control block) and the raw pointer.
    // The shared_ptr will not own the Framebuffer; it simply points at
    // it and will become invalid (dangling) the moment m_ping[m_read_idx]
    // is reset/realloced.  Callers must not retain the shared_ptr across
    // reset()/ensure_capacity() — this matches the lifetime contract of
    // the previous m_prev_framebuffer.
    //
    // Implementation: shared_ptr aliasing constructor with an empty
    // source shared_ptr.  The resulting shared_ptr holds a non-owning
    // reference to `read`.
    std::shared_ptr<Framebuffer> empty_owner;
    return std::shared_ptr<Framebuffer>(empty_owner, read);
}

Framebuffer* RendererBufferRing::read_buffer() const {
    return m_ping[m_read_idx].get();
}

Framebuffer* RendererBufferRing::write_buffer() const {
    return m_ping[m_write_idx].get();
}

void RendererBufferRing::reset() {
    m_ping[0].reset();
    m_ping[1].reset();
    m_read_idx = 0;
    m_write_idx = 1;
    m_has_committed = false;
}

} // namespace chronon3d
