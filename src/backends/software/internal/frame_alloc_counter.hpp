// SPDX-License-Identifier: MIT
//
// frame_alloc_counter.hpp — TICKET-121 (Phase 1)
//
// Internal-only frame-bound allocation counter for hot-path profiling.
//
// Thread-local backing storage.  No atomic, no global cache, no
// singleton.  Strictly scoped to `src/backends/software/` (BUILD_INTERFACE
// only) and never installed to `include/chronon3d/` per AGENTS.md Cat-2.
//
// Usage:
//   #include "internal/frame_alloc_counter.hpp"   // sibling-TU include
//   ...
//   chronon3d::internal::FrameAllocCounter::record_alloc(
//       static_cast<std::size_t>(w) * h * sizeof(Color));
//   auto fb = std::make_unique<Framebuffer>(w, h);
//
// Observability only — does NOT change the ordering or content of
// allocations.  Bit-exact compatibility with golden tests is preserved.

#pragma once

#include <cstddef>

namespace chronon3d::internal {

// Thread-local frame-bound allocation counter.
//
// Each thread that calls record_alloc() runs an independent counter.
// The counter is observability-only: it has no observable effect on
// allocation order, content, or lifetime of underlying heap objects.
//
// Designed to be a minimum-viable Phase-1 instrumentation per
// TICKET-121.  No synchronization (no std::atomic, no std::mutex).
class FrameAllocCounter {
public:
    // Record that the calling thread has allocated `bytes` bytes.
    // Used by hot-path Framebuffer wrappers pre-allocation.
    static void record_alloc(std::size_t bytes) noexcept;

    // Read the calling thread's total bytes-allocated since the last
    // reset (or thread start, whichever is most recent).
    static std::size_t current_total_bytes_thread_local() noexcept;

    // Reset the calling thread's counter to zero.  Used by tests and
    // (future) per-frame flush logic when fully wired.
    static void reset_thread_local() noexcept;
};

} // namespace chronon3d::internal
