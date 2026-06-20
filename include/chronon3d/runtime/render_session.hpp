#pragma once

// ---------------------------------------------------------------------------
// runtime/render_session.hpp
//
// Per-session rendering state — canonical placement.
//
// After the RenderSession extraction refactor ("Spostare e separare
// RenderSession", sections 8.2 + 8.3 of the architecture plan) this header
// is the canonical location of `struct RenderSession`.  The legacy
// location at `<chronon3d/core/memory/render_session.hpp>` is now a
// forwarding stub.
//
// Strict renderer-agnostic boundary:
//   - FrameArena        (per-frame temp allocator)
//   - FrameHistory      (per-frame camera + fingerprint history)
//   - DirtyHistory      (per-frame dirty/tile/reuse telemetry)
//   - RendererLayerHistory (per-layer bbox history — kept in math/ until
//     next phase when it merges into DirtyHistory.previous_layers)
//   - JobTelemetry      (per-render-job aggregate — placeholder)
// Notably ABSENT from RenderSession:
//   - RendererBufferRing, TransformScratchBuffer, graph::SceneHasher
//     These belong to the SOFTWARE backend and now live in
//     `SoftwareSessionResources` instead.
//
// Reset semantics (section 8.7):
//   - reset_frame_temporaries(): resets FrameArena only.
//   - reset_job(): resets frame_history, dirty_telemetry, layer_history,
//     telemetry.  Does NOT reset arena (job-level reset can outlive a
//     single frame's arena lifetime, so we keep the arena usable for the
//     lifetime of the in-flight frame).
//
// pre-Phase-3 the legacy `clear_per_frame()` method lived on RenderSession
// and reset everything except the arena.  Going forward, callers that
// want the same behavior should call `reset_job()`.  The legacy entry
// point is REMOVED from this header.
// ---------------------------------------------------------------------------

#include <memory>

#include <chronon3d/core/memory/arena.hpp>
#include <chronon3d/math/renderer_state.hpp>
#include <chronon3d/runtime/frame_history.hpp>
#include <chronon3d/runtime/dirty_history.hpp>
#include <chronon3d/runtime/job_telemetry.hpp>

namespace chronon3d {

/// Per-session rendering state (renderer-agnostic).
///
/// All members are default-constructible and movable.  Movability is
/// preserved by `arena_ptr` holding `FrameArena` (which contains a
/// `std::pmr::monotonic_buffer_resource` and is therefore non-movable).
struct RenderSession {
    // FrameArena is behind unique_ptr because it contains a
    // std::pmr::monotonic_buffer_resource which is non-movable.
    std::unique_ptr<FrameArena> arena_ptr{std::make_unique<FrameArena>()};

    // ── Renderer-agnostic state ──────────────────────────────────────
    FrameHistory              frame_history;
    DirtyHistory              dirty_telemetry;
    RendererLayerHistory      layer_history;
    JobTelemetry              telemetry;

    /// Convenience accessor for the frame arena.
    [[nodiscard]] FrameArena& arena() { return *arena_ptr; }
    [[nodiscard]] const FrameArena& arena() const { return *arena_ptr; }

    // ── Reset semantics (see architecture plan section 8.7) ──────────

    /// Reset per-frame temporaries ONLY: the FrameArena is wiped, so the
    /// caller can begin allocating again for the next frame.
    ///
    /// Does NOT reset frame_history / dirty_telemetry / layer_history /
    /// telemetry — those survive a per-frame boundary because the
    /// rendering loop needs them to compare against the previous frame.
    void reset_frame_temporaries() {
        if (arena_ptr) arena_ptr->reset();
    }

    /// Full per-job reset: frame_history, dirty_telemetry, layer_history,
    /// and telemetry are reset.  Does NOT reset the FrameArena (which is
    /// already covered by `reset_frame_temporaries()` if needed).
    ///
    /// Does NOT touch software-specific resources (buffer ring, scratch
    /// buffer, scene hasher) — they belong to `SoftwareSessionResources`.
    /// Use `SoftwareRenderSession::reset_job()` to reset both halves.
    void reset_job() {
        frame_history = FrameHistory{};
        dirty_telemetry = DirtyHistory{};
        layer_history = RendererLayerHistory{};
        telemetry = JobTelemetry{};
        // arena_ptr is intentionally NOT reset here — see the doc comment.
    }
};

} // namespace chronon3d
