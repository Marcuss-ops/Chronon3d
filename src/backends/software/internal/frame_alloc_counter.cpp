// SPDX-License-Identifier: MIT
//
// frame_alloc_counter.cpp — TICKET-121 (Phase 1)
// Implementation backing the header `internal/frame_alloc_counter.hpp`.

#include "internal/frame_alloc_counter.hpp"

namespace chronon3d::internal {

namespace {

// TICKET-121 (Phase 1) — strictly thread-local backing storage.
// No std::atomic needed: each thread reads its own counter and never
// races against other threads' counters.  Zero-init on first access
// is the standard semantics of `thread_local` of scalar type.
thread_local std::size_t s_alloc_bytes = 0;

} // namespace

void FrameAllocCounter::record_alloc(std::size_t bytes) noexcept {
    s_alloc_bytes += bytes;
}

std::size_t FrameAllocCounter::current_total_bytes_thread_local() noexcept {
    return s_alloc_bytes;
}

void FrameAllocCounter::reset_thread_local() noexcept {
    s_alloc_bytes = 0;
}

} // namespace chronon3d::internal
