#pragma once

// ===========================================================================
// src/backends/software/processors/text_run/text_run_processor/text_run_stages.hpp
//
// M1.5#6 internal contract — four-stage split of draw_text_run().
//
// Each stage lives in its own sub-cpp under this dir:
//   - prepare.cpp    : Stage 1 (validation + bbox + font resolve + scratch acquire)
//   - raster.cpp     : Stage 2 (tier-classify + tier render + main + crossfade +
//                            downsample → BLImage in s.img)
//   - effects.cpp    : Stage 3 (TextMaterial apply — gradient / bevel)
//   - composite.cpp  : Stage 4 (BLImage → params.fb via blend2d_bridge;
//                            releases s.img to scratch pool)
//   - scratch.cpp    : scratch pool wrappers + apply_separable_box_blur +
//                      downsample_supersampled + env-var mode toggle.
//
// All five sub-cpps + this header compile into `chronon3d_backend_software`
// (gated on CHRONON3D_ENABLE_TEXT).  Public API is unchanged:
// `chronon3d::renderer::draw_text_run(...)` retains the same signature.
//
// AGENTS.md v0.1 freeze Cat-3 internal-only — lives strictly in
// src/backends/software/processors/text_run/text_run_processor/.
// NOT promoted to include/chronon3d/.  Zero new public symbols.

#include <chronon3d/backends/software/software_processor_context.hpp>
#include <chronon3d/backends/software/text_run_processor.hpp>
#include <chronon3d/backends/text/text_render_resources.hpp>
#include <chronon3d/text/text_run_layout.hpp>
#include <blend2d.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace chronon3d::renderer::text_run_stages {

// ── TextRasterSpace — unified supersampling + offset ─────────────────────
//
// TICKET-TEXT-CLEANUP-3: replaces the separate ss_offset_x / ss_offset_y
// fields with a single object that encapsulates the rasterization coordinate
// system.  `scale` is the supersampling factor, `offset_x/y` are the
// surface-space offsets (already scaled by ss).  Use to_surface_matrix()
// to combine a glyph matrix with the raster space.
struct TextRasterSpace {
    int   scale{1};
    float offset_x{0.0f};
    float offset_y{0.0f};
};

/// Combine a glyph matrix with the raster space to produce a surface-local
/// matrix.  Replaces the manual `glyph_mat.translate(-offset_x, -offset_y)`
/// pattern.
[[nodiscard]] inline BLMatrix2D to_surface_matrix(
    const BLMatrix2D& glyph_mat,
    const TextRasterSpace& rs
) {
    BLMatrix2D m = glyph_mat;
    m.translate(-rs.offset_x, -rs.offset_y);
    return m;
}

// Number of blur tiers for per-glyph blur classification.
// Invariant: matches the size of kBlurTierRadii below + BlurTiers::value_type.
inline constexpr std::size_t kNumBlurTiers = 5;

// Canonical tier radii.  Single source of truth used by both raster.cpp
// (per-tier blur application) and (if needed in future) effects.cpp for
// material-aware blur radii.  Values verbatim from the documented tier
// block in src/backends/software/processors/text_run/text_run_processor.cpp
// pre-M1.5#6 (TICKET-Phase4-BlurTierRadii restoration commit).
// (Single-brace aggregate init: std::array<T,N> uses {a,b,c,...}, not
//  {{a,b,c,...}} which would nest an array-brace inside the outer.)
inline constexpr std::array<i32, kNumBlurTiers> kBlurTierRadii = {
    0, 2, 7, 13, 20
};

// Per-tier glyph index classification — produced by build_blur_tiers()
// in raster.cpp, consumed by the tiered-rendering passes.
using BlurTiers = std::array<std::vector<std::uint32_t>, kNumBlurTiers>;

// ── TextRunStageState — cross-stage mutable state ─────────────────────────
//
// Lives on the orchestrator's stack for the duration of one draw_text_run()
// call.  prepare.cpp populates the metadata fields + acquires s.scratch_handle;
// raster.cpp fills s.img + s.glyphs_drawn; effects.cpp mutates s.img;
// composite.cpp consumes s.img + releases it.
//
// Hard rules:
//   - scratch_handle is RAII-owned; release on scope exit, no manual cleanup
//     in any stage.  TSan-clean across concurrent calls (FASE 3 invariant).
//   - span_handles / span_fonts / per_glyph_span_idx filled by prepare only.
//   - img is mutated by raster (set), effects (material apply), composite
//     (consume + release).  Never realloc'd by effects/composite.
//   - glyphs_drawn is the cross-stage counter — composite updates it last.
struct TextRunStageState {
    TextScratchManager::Handle scratch_handle;

    // Resolved per-span fonts (Phase 1.4 multi-font path or single-font alias).
    std::vector<FontFaceHandle>            span_handles;
    std::vector<BLFont>                    span_fonts;
    std::vector<std::size_t>               per_glyph_span_idx;

    // Glyph bbox (run-local image extent, includes padding).
    float min_x{1e10f};
    float min_y{1e10f};
    float max_x{-1e10f};
    float max_y{-1e10f};

    // Image dimensions + run-local offset.
    int   img_w{0};
    int   img_h{0};
    float offset_x{0.0f};
    float offset_y{0.0f};

    // Supersampling (FASE 3b) — pre-computed by prepare so the raster
    // pass can route through the resolved factor without recomputing.
    // TICKET-TEXT-CLEANUP-3: ss, ss_offset_x, ss_offset_y replaced by
    // a single TextRasterSpace.  ss_img_w / ss_img_h remain as cached
    // image dimensions (used extensively for surface allocation).
    TextRasterSpace raster_space;
    int   ss_img_w{0};
    int   ss_img_h{0};

    // Tier pre-classification (O(G) over `active` side and optional crossfade).
    BlurTiers active_tiers{};
    BlurTiers crossfade_tiers{};

    // All-active-glyphs index array (used by the shadow pass; no tiering).
    std::vector<std::uint32_t> all_active_glyphs;

    // Final composed BLImage (mutated raster → effects, consumed composite).
    BLImage img;
    std::size_t glyphs_drawn{0};

    // TICKET-TEXT-CLEANUP-4: silent_success_empty removed.
    // Text that appears off-canvas is still rendered — the bbox
    // approximation may be wrong, and silently skipping visible text
    // is worse than rendering one extra off-canvas frame.
};

// ── Stage function signatures ──────────────────────────────────────────────
//
// Each stage returns graph::RenderOpResult.  Orchestrator early-returns on
// error and continues on Outcome.  All four stages execute strictly in order:
//   prepare → raster → effects → composite
//
// Three stage error-code patterns:
//   1) InvalidInput    — bad shape/fb/null resources (terminal return).
//   2) ExecutionFailure — font face failed to load / scratch acquire failed.
//   3) Outcome{N}      — silent success (0 glyphs drawn).

[[nodiscard]] graph::RenderOpResult prepare_text_run(
    const SoftwareProcessorContext& rctx,
    const TextRunDrawParams&        params,
    TextRunStageState&              s);

[[nodiscard]] graph::RenderOpResult rasterize_prepared_run(
    const SoftwareProcessorContext& rctx,
    const TextRunDrawParams&        params,
    TextRunStageState&              s);

[[nodiscard]] graph::RenderOpResult apply_text_run_effects(
    const SoftwareProcessorContext& rctx,
    const TextRunDrawParams&        params,
    TextRunStageState&              s);

[[nodiscard]] graph::RenderOpResult composite_text_run(
    const SoftwareProcessorContext& rctx,
    TextRunDrawParams&              params,
    TextRunStageState&              s);

// ── Scratch pool helpers ────────────────────────────────────────────────────
//
// Defined in scratch.cpp.  Forward-declared here so prepare.cpp /
// raster.cpp / composite.cpp can call them through the namespace without
// needing to include the helper file directly (single source of truth).

[[nodiscard]] TextScratchManager::Handle acquire_scratch_handle(
    const SoftwareProcessorContext& rctx);

[[nodiscard]] BLImage acquire_surface(
    TextRunStageState& s,
    int w, int h,
    std::uint32_t fmt = BL_FORMAT_PRGB32);

void release_surface(
    TextRunStageState& s,
    BLImage img);

void apply_separable_box_blur(
    BLImage& image, int radius, TextRunStageState& s);

void downsample_supersampled(
    BLImage& dst, const BLImage& src, int ss,
    TextRunStageState& s);

[[nodiscard]] bool force_parallel_mode() noexcept;

} // namespace chronon3d::renderer::text_run_stages
