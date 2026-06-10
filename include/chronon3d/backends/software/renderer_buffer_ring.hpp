#pragma once

// ── RendererBufferRing ───────────────────────────────────────────────────────
//
// Encapsulates the ping-pong framebuffer ring used by SoftwareRenderer.
//
// Two persistent framebuffers are alternated each frame: ClearNode writes to
// the "write" slot, while the writer thread reads from the "read" slot.
// Because write and read are always exclusive, ClearNode never needs a COW
// detach.  `previous_frame()` returns a non-owning shared_ptr-wrapping view
// of the most recent read ping for the early-exit path compatibility.
//
// The class owns the Framebuffer memory outright (via std::unique_ptr).  No
// raw pointer + no-op deleter, no dangling risks.  All public methods are
// thread-safe at the level required by the SoftwareRenderer (single-writer
// per frame; all access happens on the rendering thread).
//
// ── Usage ────────────────────────────────────────────────────────────────────
//   RendererBufferRing ring;
//   ring.ensure_capacity(width, height);  // called at frame start
//   Framebuffer* write = ring.acquire_write();  // exclusive target
//   ... render frame ...
//   ring.commit_written_frame();          // repoints prev to the new read
//   auto prev = ring.previous_frame();    // shared view (no ownership)
//
// ── Reset semantics ──────────────────────────────────────────────────────────
//   reset() releases all buffers (e.g. on resolution change or shutdown).
//   After reset() the ring is empty and must be re-initialized with
//   ensure_capacity() before next use.

#include <chronon3d/core/memory/framebuffer_handle.hpp>
#include <array>
#include <memory>

namespace chronon3d {

class RendererBufferRing {
public:
    RendererBufferRing() = default;
    ~RendererBufferRing() = default;

    // Non-copyable (unique_ptr members).  Movable: the std::array of
    // unique_ptr transfers ownership correctly via the defaulted move ops.
    RendererBufferRing(const RendererBufferRing&) = delete;
    RendererBufferRing& operator=(const RendererBufferRing&) = delete;
    RendererBufferRing(RendererBufferRing&&) noexcept = default;
    RendererBufferRing& operator=(RendererBufferRing&&) noexcept = default;

    /// Allocate or reallocate both ping buffers to (at least) the requested
    /// size.  Existing buffers are reused if they're large enough; otherwise
    /// the old storage is released and fresh buffers are allocated.
    ///
    /// Safe to call multiple times across frames; resolution changes are
    /// handled transparently.
    void ensure_capacity(int width, int height);

    /// Returns the exclusive write slot for the current frame.  Cleared to
    /// transparent.  The caller is responsible for swapping (via
    /// commit_written_frame()) once the frame is complete.
    [[nodiscard]] Framebuffer* acquire_write();

    /// Swap read/write indices and repoint the "previous frame" alias to
    /// the newly read ping.  Call this once at the end of each frame.
    void commit_written_frame();

    /// Returns a non-owning shared_ptr<Framebuffer> aliasing the most
    /// recently committed read ping.  Used by the early-exit fast path
    /// which only needs a borrowed view.  May be empty (nullptr) if
    /// commit_written_frame() has not yet been called.
    [[nodiscard]] std::shared_ptr<Framebuffer> previous_frame() const;

    /// Returns the read ping directly.  The pointer is stable for the
    /// lifetime of the ring (or until reset()).
    [[nodiscard]] Framebuffer* read_buffer() const;

    /// Returns the write ping directly (for callers that need the raw
    /// pointer instead of the shared_ptr view).
    [[nodiscard]] Framebuffer* write_buffer() const;

    /// Returns the index (0 or 1) currently designated as the read slot.
    /// Useful for logging and debugging.
    [[nodiscard]] int read_index() const { return m_read_idx; }

    /// Returns the index (0 or 1) currently designated as the write slot.
    [[nodiscard]] int write_index() const { return m_write_idx; }

    /// Direct access to the raw Framebuffer pointer for ping slot `idx` (0 or 1).
    /// Returns nullptr if the slot has not been allocated.  The pointer is
    /// stable until the next ensure_capacity() or reset() call.
    [[nodiscard]] Framebuffer* ping(int idx) const { return m_ping[idx].get(); }

    /// Release both ping buffers (e.g. on resolution change or shutdown).
    /// After reset() the ring is empty and must be re-initialized.
    void reset();

    /// True if no ping has been committed yet (i.e. previous_frame() is null).
    [[nodiscard]] bool has_previous_frame() const { return m_has_committed; }

private:
    // Owned storage — unique_ptr guarantees a single owner and a proper
    // deleter, eliminating the "raw pointer + no-op deleter" pattern.
    std::array<std::unique_ptr<Framebuffer>, 2> m_ping;
    int m_read_idx{0};
    int m_write_idx{1};
    bool m_has_committed{false};
};

} // namespace chronon3d
