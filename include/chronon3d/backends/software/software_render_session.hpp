#pragma once

// ---------------------------------------------------------------------------
// backends/software/software_render_session.hpp
//
// Combined renderer + software-backend session for a single render job.
//
// Per design ("Spostare e separare RenderSession", section 8.5):
//
//   struct SoftwareRenderSession {
//       RenderSession            common;       // renderer-agnostic
//       SoftwareSessionResources software;     // CPU-specific
//   };
//
// This wrapper composes:
//   - `common`  (`<chronon3d/runtime/render_session.hpp>`) holds the
//               renderer-agnostic state: FrameArena, frame_history,
//               dirty_history, layer_history, telemetry.
//   - `software` (`<chronon3d/backends/software/software_session_resources.hpp>`)
//               holds CPU-specific resources: buffer_ring,
//               transform_scratch, scene_hasher.
//
// Reset composition:
//   - reset_frame_temporaries() forwards to BOTH halves so a per-frame
//     boundary wipes the arena AND the transform scratch (but keeps the
//     previous-frame buffer ring).
//   - reset_job() forwards to BOTH halves for a full job reset.
//
// The `SoftwareRenderer::m_session` field is NOT yet migrated to this
// wrapper — that migration is phase 5.  Until phase 5,
// `SoftwareRenderer` holds `RenderSession m_session` and a separate
// `SoftwareSessionResources m_software_resources` field, and this
// wrapper is the end-state type that phase 5 will adopt.
// ---------------------------------------------------------------------------

#include <chronon3d/runtime/render_session.hpp>
#include <chronon3d/backends/software/software_session_resources.hpp>

namespace chronon3d {

/// Combined renderer + software-backend session for a single render job.
///
/// Same copy/move policy as `SoftwareSessionResources`: copy is
/// explicitly deleted (the `SoftwareSessionResources` member is
/// non-copyable), move is explicitly defaulted so future changes to a
/// subobject cannot silently drop the implicit move and break
/// `SoftwareRenderer`'s defaulted move constructor.
struct SoftwareRenderSession {
    // ── Renderer-agnostic part ──────────────────────────────────────────
    RenderSession            common;

    // ── Software-backend-specific part ─────────────────────────────────
    SoftwareSessionResources software;

    SoftwareRenderSession() = default;
    SoftwareRenderSession(const SoftwareRenderSession&) = delete;
    SoftwareRenderSession& operator=(const SoftwareRenderSession&) = delete;
    SoftwareRenderSession(SoftwareRenderSession&&) noexcept = default;
    SoftwareRenderSession& operator=(SoftwareRenderSession&&) noexcept = default;

    /// Per-frame reset: empty the arena (canonical) and the software
    /// scratch (buffer ring NOT touched — it's needed for the previous
    /// frame's content until the next frame's commit).
    void reset_frame_temporaries() {
        common.reset_frame_temporaries();
        software.reset_frame_temporaries();
    }

    /// Full per-job reset: history + dirty + telemetry + buffer ring +
    /// transform scratch + scene hasher.  Persistent runtime caches
    /// (image cache, node cache, framebuffer pool, compiled graph cache)
    /// are intentionally NOT touched.
    void reset_job() {
        common.reset_job();
        software.reset_job();
    }
};

} // namespace chronon3d
