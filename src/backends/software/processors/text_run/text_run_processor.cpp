// ═══════════════════════════════════════════════════════════════════════════
// src/backends/software/processors/text_run/text_run_processor.cpp — M1.5#6
// ═══════════════════════════════════════════════════════════════════════════
//
// THIN ORCHESTRATOR (M1.5#6): the previous 1004-LOC monolith has been
// decomposed into 5 single-responsibility sub-cpps under
// `text_run_processor/`:
//   - prepare.cpp   : validation + bbox + font resolve + scratch handle
//   - raster.cpp    : tier-classify + shadow stack + main tiered +
//                     crossfade side + downsample → s.img
//   - effects.cpp   : s.img TextMaterial apply
//   - composite.cpp : s.img → params.fb (BL bridge) + counters
//   - scratch.cpp   : scratch pool wrappers + helpers (blur, downsample,
//                     env-var mode toggle)
//
// The orchestrator now reads as a 4-stage pipeline:
//   1. prepare_text_run        (Stage 1: metadata + scratch acquire)
//   2. rasterize_prepared_run  (Stage 2: produce s.img)
//   3. apply_text_run_effects  (Stage 3: material applied IN-PLACE)
//   4. composite_text_run      (Stage 4: blend to params.fb + counters)
//
// Public surface UNCHANGED: `draw_text_run(SoftwareProcessorContext&,
// TextRunDrawParams&) → RenderOpResult` retains its existing signature.
//
// Scratch invariant: the per-call TextScratchManager::Handle is acquired
// by prepare.cpp (the ONLY place) and released RAII-style when `s`
// (its TextRunStageState handle field) goes out of scope.  NO vector is
// recreated per draw.
//
// Counter invariant: `text_glyphs_rasterized` is updated by composite.cpp
// AFTER the BL bridge pixel write, preserving the pre-M1.5#6 observable
// ordering for downstream listeners.
//
// AGENTS.md v0.1 freeze Cat-3 internal — orchestrator is internal-only.

#include <chronon3d/backends/software/text_run_processor.hpp>
#include <chronon3d/text/text_run_geometry.hpp>
#include "text_run_processor/text_run_stages.hpp"

namespace chronon3d::renderer {

graph::RenderOpResult draw_text_run(
    const SoftwareProcessorContext& rctx,
    TextRunDrawParams&              params
) {
    // Bring the text_run_stages namespace's stage fns + TextRunStageState
    // into scope via a namespace alias (more explicit than `using namespace`).
    namespace stages = chronon3d::renderer::text_run_stages;

    stages::TextRunStageState s;
    CHRONON_ZONE_C("text_run_draw", trace_category::kText);

    // Stage 1 — prepare (validation, scratch acquire, font resolve, bbox, ss)
    if (auto r = stages::prepare_text_run(rctx, params, s); !r.ok()) {
        return r;
    }

    // Stage 2 — rasterize (tier pre-classify → BLImage; nullifies silent-empty)
    if (auto r = stages::rasterize_prepared_run(rctx, params, s); !r.ok()) {
        return r;
    }

    // Stage 3 — effects (TextMaterial apply on s.img, in place)
    if (auto r = stages::apply_text_run_effects(rctx, params, s); !r.ok()) {
        return r;
    }

    // Stage 4 — composite (BL bridge → params.fb; counters)
    return stages::composite_text_run(rctx, params, s);
}

// NOTE: the `TICKET-118` removal of the dummy `TextRunProcessor` shape
// factory stays in effect — see CHANGELOG.md#M1.5#118 and the old end-of-file
// note in the pre-M1.5#6 monolith (kept in archived change logs).

// ═══════════════════════════════════════════════════════════════════════════
// detail::bucket_radius_for_tier — re-exported from public text_run_processor.hpp
// ═══════════════════════════════════════════════════════════════════════════
//
// Still lives in include/chronon3d/backends/software/text_run_processor.hpp
// (inline).  raster.cpp and raster code paths call it via
// `render_tier_to_image` which imports `detail::bucket_radius_for_tier` from
// the public header — no definition here.

} // namespace chronon3d::renderer
