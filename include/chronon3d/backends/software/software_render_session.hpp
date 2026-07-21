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
//   - `common`  (`<chronon3d/internal/runtime/render_session.hpp>`) holds the
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
// `SoftwareRenderer::m_session` IS this wrapper — the migration to
// `SoftwareRenderSession` (composition of `RenderSession common` +
// `SoftwareSessionResources software`) is complete.
// ---------------------------------------------------------------------------

#include <chronon3d/internal/runtime/render_session.hpp>
#include <chronon3d/backends/software/software_session_resources.hpp>
#include <chronon3d/text/text_run_shape.hpp>
#include <memory>
#include <string>
#include <vector>

namespace chronon3d {

/// Lightweight snapshot of a TextRunNode captured after the render graph
/// is built.  Used by diagnostic tools (e.g. `chronon3d_cli inspect-text`)
/// to audit text visibility without re-evaluating the scene.
struct TextRunAuditSnapshot {
    std::string name;
    std::shared_ptr<TextRunShape> shape;
    Mat4 world_matrix;
    Rect predicted_bbox;
    Rect clip_rect;
};

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

    // ── Diagnostic text-run snapshots (last rendered frame) ─────────────
    std::vector<TextRunAuditSnapshot> text_audit_snapshots;

    // ── Software-backend-specific part ─────────────────────────────────
    SoftwareSessionResources software;

    SoftwareRenderSession() = default;
    SoftwareRenderSession(const SoftwareRenderSession&) = delete;
    SoftwareRenderSession& operator=(const SoftwareRenderSession&) = delete;
    SoftwareRenderSession(SoftwareRenderSession&&) noexcept = default;
    SoftwareRenderSession& operator=(SoftwareRenderSession&&) noexcept = default;

    // WP-3 PR 3.1 / 3.2 — convenience proxies for the canonical
    // per-session state engines.  Required because the PR 3.3 test
    // lattice addresses `sr.scene_hasher()` / `sr.program_store()`
    // directly on the composition (the duplicate `software.scene_hasher`
    // that used to live on the backend-specific half is REMOVED; the
    // canonical home is the common half).  Mirroring these proxies on
    // the composition also matches downstream callers'
    // `sw_renderer->scene_hasher()` invocation pattern (the inner
    // chain `sw_renderer->software_session().scene_hasher()` must
    // compile without forcing callers to spell `.common`).
    [[nodiscard]] ::chronon3d::graph::SceneHasher&       scene_hasher()       { return common.scene_hasher(); }
    [[nodiscard]] const ::chronon3d::graph::SceneHasher& scene_hasher() const { return common.scene_hasher(); }
    [[nodiscard]] ::chronon3d::graph::SceneProgramStore&       program_store()       { return common.program_store(); }
    [[nodiscard]] const ::chronon3d::graph::SceneProgramStore& program_store() const { return common.program_store(); }

    /// Per-frame reset: empty the arena (canonical) and the software
    /// scratch (buffer ring NOT touched — it's needed for the previous
    /// frame's content until the next frame's commit).
    /// Diagnostic text-audit snapshots are per-frame state (snapshot of
    /// the LATEST rendered frame for diagnostic tools like
    /// `chronon3d_cli inspect-text`); cleared here so the next frame's
    /// render starts with a clean snapshot container.
    void reset_frame_temporaries() {
        common.reset_frame_temporaries();
        software.reset_frame_temporaries();
        text_audit_snapshots.clear();
    }

    /// Full per-job reset: history + dirty + telemetry + buffer ring +
    /// transform scratch + scene hasher.  Persistent runtime caches
    /// (image cache, node cache, framebuffer pool, compiled graph cache)
    /// are intentionally NOT touched.  Diagnostic text-audit snapshots
    /// are NOT cleared here — they belong to per-frame lifetime and
    /// are managed exclusively by reset_frame_temporaries(), so
    /// re-running render() within the same job preserves the
    /// last-frame visibility for `chronon3d_cli inspect-text`.
    void reset_job() {
        common.reset_job();
        software.reset_job();
    }
};

} // namespace chronon3d
