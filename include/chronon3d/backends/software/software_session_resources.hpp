#pragma once

// ---------------------------------------------------------------------------
// backends/software/software_session_resources.hpp
//
// Per-render-job resources that are CPU/software-backend specific.
//
// After the RenderSession extraction (architecture plan section 8.5) this
// header holds the per-job state that used to live directly on
// `RenderSession` as members.  Moving it out lets `RenderSession.common`
// stay renderer-agnostic (no `backends/software/*` includes).
//
// Members:
//   - RendererBufferRing      buffer_ring        ping-pong framebuffers
//   - TransformScratchBuffer  transform_scratch  transform-node scratch FB
//
// Reset semantics:
//   - reset_frame_temporaries(): resets transform_scratch (so the next
//     frame starts fresh scratch state).  Does NOT reset the buffer ring
//     (which holds the previous frame's output and must survive until
//     commit_written_frame()).
//   - reset_job(): resets buffer_ring + transform_scratch in full.  Use
//     at the start of an unrelated render job.
//
// Cache persistenti (image cache, node cache, pool) NON vengono toccate
// da reset_job — le risorse di sessione sono solo ciò che è elencato
// sopra.
//
// WP-3 PR 3.1 — the `graph::SceneHasher scene_hasher` member was
// REMOVED.  The canonical scene hasher is now a per-session value
// member on `RenderSession::scene_hasher`.  `SoftwareRenderSession`
// (which composes `RenderSession common + SoftwareSessionResources
// software`) reaches the scene hasher through
// `session.common.scene_hasher()`.  This struct no longer needs an
// include of `<chronon3d/render_graph/core/scene_hasher.hpp>`
// (TICKET-013 boundary invariant restored for this header).
// ---------------------------------------------------------------------------

#include <chronon3d/backends/software/buffer_ring.hpp>
#include <chronon3d/backends/software/scratch_buffer.hpp>

namespace chronon3d {

/// Software-backend specific resources attached to a single render job.
///
/// All members own their memory outright (RAII).  Copy is explicitly
/// deleted (all subobjects are non-copyable); move is explicitly
/// defaulted so future additions to the struct (or changes to a member's
/// copy semantics) cannot silently delete the implicit move and break
/// `SoftwareRenderer`'s defaulted move constructor.
struct SoftwareSessionResources {
    // ── Per-frame scratch and previous-frame ownership ───────────────────
    RendererBufferRing      buffer_ring;
    // Field is named `scratch_buffer` (NOT `transform_scratch`) on purpose
    // so the public accessor `SoftwareRenderer::scratch_buffer()` reads
    // symmetrically with the field.  The TYPE is `TransformScratchBuffer`
    // because of the design (architecture plan section 8.5).
    TransformScratchBuffer  scratch_buffer;

    SoftwareSessionResources() = default;
    SoftwareSessionResources(const SoftwareSessionResources&) = delete;
    SoftwareSessionResources& operator=(const SoftwareSessionResources&) = delete;
    SoftwareSessionResources(SoftwareSessionResources&&) noexcept = default;
    SoftwareSessionResources& operator=(SoftwareSessionResources&&) noexcept = default;

    /// Reset per-frame temporaries ONLY: the transform scratch buffer is
    /// released (it gets reallocated lazily on the next frame's
    /// `slot_view(width, height)` call).  The buffer ring is preserved
    /// because its previous-frame data must remain valid until the
    /// next frame's commit.
    void reset_frame_temporaries() {
        scratch_buffer.reset();
        // buffer_ring intentionally preserved: holds previous frame FB.
        // WP-3 PR 3.1: scene_hasher reset no longer happens here —
        // canonical scene_hasher is on RenderSession (per-session).
    }

    /// Full job-level reset: release the previous frame buffer ring
    /// and release the transform scratch.  Persistent caches
    /// (image cache, node cache, framebuffer pool) are NOT touched
    /// here — those belong to the runtime, not the session.
    /// WP-3 PR 3.1: scene_hasher reset no longer happens here
    /// (canonical scene_hasher is on RenderSession).
    void reset_job() {
        buffer_ring.reset();
        scratch_buffer.reset();
    }
};

} // namespace chronon3d
