#pragma once

// ---------------------------------------------------------------------------
// backends/software/software_session_resources.hpp
//
// Per-render-job resources that are CPU/software-backend specific.
//
// After the runtime::JobSession extraction (architecture plan section 8.5) this
// header holds the per-job state that used to live directly on
// `runtime::JobSession` as members.  Moving it out lets `runtime::JobSession.common`
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
// Scene-hash and scene-program state is software graph execution state and
// follows this software-session owner rather than the renderer-agnostic job.
// ---------------------------------------------------------------------------

#include <chronon3d/backends/software/buffer_ring.hpp>
#include <chronon3d/backends/software/scratch_buffer.hpp>
#include <chronon3d/backends/software/depth_buffer_pool.hpp>
#include <chronon3d/backends/software/effects/per_pixel_dof.hpp>
#include <chronon3d/internal/render_graph/core/scene_hasher.hpp>
#include <chronon3d/internal/render_graph/cache/scene_program_store.hpp>
#include <chronon3d/backends/text/text_render_resources.hpp>  // M1.5#7 — complete-type visibility for std::unique_ptr<TextRenderResources> deleter
#include <memory>  // std::unique_ptr (M1.5#7 RAII for text_resources)
#include <algorithm>
#include <vector>
#include <cstdint>
#include <string>

// M1.5#7 — `TextRenderResources` is the per-session aggregator of
// font + glyph + raster + scratch caches.  Forward-declared here so
// `SoftwareSessionResources` can hold a value-member without pulling
// the heavy `blend2d.h` header (the struct itself is declared in the
// M1.5#7 split header; construction is default and is the only
// path the canonical aggregator takes).
// NOTE: as of M1.5#8 (TICKET-GATE-10-PHASE-4-BLACK diagnostic),
// the forward declaration block was replaced with a full
// `<chronon3d/backends/text/text_render_resources.hpp>` include
// because `std::unique_ptr<TextRenderResources>` with the default
// deleter requires sizeof(T) at every destructor instantiation site;
// without the include, libstdc++'s `unique_ptr.h:90 static_assert`
// fires when the dtor is used in any TU that only sees the
// forward-declaration.  The forward-decl is now redundant because
// the include provides the complete type.

namespace chronon3d {

struct CompiledComposition;

/// One shared transient workspace for scalar effect processors. Sequential
/// effects reuse these buffers; no effect-specific pool is introduced.
struct EffectScratchResources {
    std::unique_ptr<Framebuffer> framebuffer;
    std::unique_ptr<Framebuffer> framebuffer_b;
    std::vector<float> original_alpha;
    std::vector<float> row_src, row_dst, col_src, col_dst;
    // Directional blur taps are packed as dx, dy, weight triples.
    std::vector<float> directional_taps;
    int width{0};
    int height{0};

    void ensure_size(int requested_width, int requested_height) {
        if (requested_width <= 0 || requested_height <= 0) return;
        if (!framebuffer || framebuffer->width() != requested_width ||
            framebuffer->height() != requested_height) {
            framebuffer = std::make_unique<Framebuffer>(
                requested_width, requested_height, false);
        }
        if (!framebuffer_b || framebuffer_b->width() != requested_width ||
            framebuffer_b->height() != requested_height) {
            framebuffer_b = std::make_unique<Framebuffer>(
                requested_width, requested_height, false);
        }
        width = std::max(width, requested_width);
        height = std::max(height, requested_height);
        row_src.resize(static_cast<std::size_t>(width));
        row_dst.resize(static_cast<std::size_t>(width));
        col_src.resize(static_cast<std::size_t>(height));
        col_dst.resize(static_cast<std::size_t>(height));
        original_alpha.resize(static_cast<std::size_t>(width) *
                              static_cast<std::size_t>(height));
    }

    void reset() {
        framebuffer.reset();
        framebuffer_b.reset();
        original_alpha.clear();
        row_src.clear(); row_dst.clear(); col_src.clear(); col_dst.clear();
        directional_taps.clear();
        width = 0; height = 0;
    }
};

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

    /// Reusable depth buffer for mesh rasterization.  Eliminates the
    /// per-frame `std::vector<float>` allocation in mesh processor draw().
    /// Reset via `reset_job()` → `reset_temporal_history()` path.
    DepthBufferPool depth_buffer_pool;
    renderer::DofScratchBuffers dof_scratch;
    EffectScratchResources effect_scratch;
    graph::SceneHasher scene_hasher_state{};
    std::unique_ptr<graph::SceneProgramStore> program_store_state{
        std::make_unique<graph::SceneProgramStore>()};

    // Authoring adapters are software-renderer compatibility state, not
    // generic JobSession state. The compiled-program path does not consult
    // these fields.
    const CompiledComposition* prepared_composition{nullptr};
    std::string authoring_composition_name{};
    std::uint64_t authoring_composition_identity{0};
    std::shared_ptr<const CompiledComposition> authoring_compiled_composition;

    // M1.5#7 — TextRenderResources aggregated value member.  This is
    // the CANONICAL OWNER of all text-backend caches (font face +
    // FreeType face + glyph atlas + raster cache + scratch pool).  It
    // lives on the SOFTWARE side of the session (not on the
    // engine-generic `runtime::JobSession`) because it pulls in
    // `<blend2d.h>` and `<ft2build.h>` — backend-specific includes
    // that would violate the WP-3 dependency-direction invariant.
    //
    // Default-constructible; the constructor of `TextRenderResources`
    // is `= default` (no special init).  `bl_faces`/`ft_faces` are
    // empty; first font access lazily `BLFontFace::createFromFile` /
    // `FT_Init_FreeType` via the M1.5#7 lazy-init pattern in each
    // sub-class.
    std::unique_ptr<TextRenderResources> text_resources;

    SoftwareSessionResources();  // -- defined OOL in software_session_resources.cpp
                                 //    -- sets text_resources = new TextRenderResources()
    ~SoftwareSessionResources(); // -- defined OOL — delete text_resources
    SoftwareSessionResources(const SoftwareSessionResources&) = delete;
    SoftwareSessionResources& operator=(const SoftwareSessionResources&) = delete;
    SoftwareSessionResources(SoftwareSessionResources&&) noexcept = default;
    SoftwareSessionResources& operator=(SoftwareSessionResources&&) noexcept = default;

    /// Reset per-frame temporaries ONLY: the transform scratch buffer is
    /// INTENTIONALLY PRESERVED across frames so its rounded bucket size
    /// survives (avoids per-frame new/delete churn when animated
    /// transforms vary the output size by a few pixels).  The first
    /// frame of a job may still lazily allocate via `slot_view()`; the
    /// scratch is reused on all subsequent frames until `reset_job()`
    /// is called.  The buffer ring is preserved because its previous-
    /// frame data must remain valid until the next frame's commit.
    void reset_frame_temporaries() {
        // scratch_buffer intentionally preserved.
        // buffer_ring intentionally preserved: holds previous frame FB.
        // Scene/program caches persist across frame boundaries.
    }

    /// Reset temporal/session resources without touching runtime caches.
    /// The previous-frame ring and transform scratch belong to temporal
    /// history, not to compiled topology or frame-value caches.
    void reset_temporal_history() {
        buffer_ring.reset();
        scratch_buffer.reset();
        depth_buffer_pool.reset();
        effect_scratch.reset();
    }

    /// Full job-level reset. Persistent caches (image cache, node cache,
    /// framebuffer pool) are NOT touched here — those belong to the runtime.
    /// Reset software-owned authoring state at the full job boundary.
    void reset_job();
};

} // namespace chronon3d
