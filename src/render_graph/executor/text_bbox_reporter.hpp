#pragma once

// ============================================================================
// text_bbox_reporter.hpp — per-session reporter for text-bbox expansions.
//
// Replaces the process-wide `static std::atomic<bool>` warn-once flag in
// text_bbox_reconcile.cpp with a per-session instance.  Each graph
// execution owns its own reporter, so warnings are deduplicated within a
// single session but are emitted again for the next session — exactly the
// behaviour needed for regression tests and for per-run diagnostics.
//
// The reporter is thread-safe: execute_single_node may run nodes in parallel
// via the scheduler, so the internal flag is a std::atomic<bool>.
// ============================================================================

#include <atomic>

namespace chronon3d::graph {

struct TextBboxReporter {
    std::atomic<bool> warned{false};

    /// Mark that a warning has been emitted.  Returns true exactly once
    /// per session (the first caller wins).  Thread-safe.
    bool mark_warned() noexcept {
        return warned.exchange(true, std::memory_order_acquire) == false;
    }

    /// Query whether a warning has already been emitted this session.
    bool has_warned() const noexcept {
        return warned.load(std::memory_order_relaxed);
    }

    /// Reset the reporter (useful in tests).
    void reset() noexcept { warned.store(false, std::memory_order_release); }
};

} // namespace chronon3d::graph
