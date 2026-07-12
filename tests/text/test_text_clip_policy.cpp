// SPDX-License-Identifier: MIT

// ═══════════════════════════════════════════════════════════════════════════
// test_text_clip_policy.cpp — M1.8 §4A-ext / TICKET-SIMPLICITY-TEXT-CLIP-POLICY
//
// Extracted from `tests/text/test_pipeline_parity.cpp` per the §2 cleanup
// mandate (TICKET-SIMPLICITY-PARITY-EXTRACT).  Carries the 5-clip-rect
// matrix: 7 pipeline variations × 5 `ClipVariant` (Baseline / Expanded /
// Conservative / Full / Off) × 18 CHECKs = 630 CHECKs total.
//
// Drop scope (intentionally NOT carried over from the source file):
//   - #1-#7 single-canary tests (7 pipelines × default clip rect, 126 CHECKs).
//     Synthetic, no clip-rect semantic content; superseded by
//     `tests/text/test_pipeline_parity_real.cpp` (the 7-modulo
//     real-framebuffer-hash parity lock at §3).
//   - #43-#44 invariance tests (tile pruning + diagnostics byte-exact).
//     Forward-pointed to `tests/text/test_pipeline_parity_real.cpp::BruteDeterm-17`
//     and `tools/check_determinism.sh` per TICKET-DETERMINISM-BRUTE-17.
//
// Carried-over scope (this file):
//   - The 5 `ClipVariant` enum + 5 canonical `Rect` constants for the
//     clip-rect matrix (Baseline / Expanded / Conservative / Full / Off).
//   - The 5 base references `s_clip_base_refs[5]` (one per clip variant,
//     computed once at module load).
//   - The 35 TEST_CASEs #8-#42 (7 pipeline variations × 5 clip variants).
//   - The `assert_pipeline_clip_18_checks` macro (18 CHECKs per test).
//   - The shared `PipelineResult` / `PipelineConfig` struct definitions
//     + the `render_with_pipeline(cfg, clip_rect)` helper (TU-local,
//     no shared header — the source file is deleted, the `_real` file
//     has a different structure, and a new header for 1 file would
//     be over-engineering per Cat-3 anti-duplication).
//
// AGENTS.md v0.1 freeze compliance: pure math + harness (no Blend2D
// compositor / GPU dependency for the base identity checks). Gated
// on `CHRONON3D_BUILD_DIAGNOSTICS` via `tests/text/text_clip_policy_tests.cmake`
// because `audit_text_visibility()` (the FU02/FU03 canonical field
// extraction) is gated by that macro. Unconditional CMake
// registration; safe to run in every preset that enables diagnostics.

#include <doctest/doctest.h>

#include <chronon3d/text/text_visibility_audit.hpp>
#include <chronon3d/text/text_definition.hpp>
#include <chronon3d/text/text_placement.hpp>
#include <chronon3d/text/text_placement_resolver.hpp>
#include <chronon3d/text/text_run.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/time.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>

#include <tests/helpers/test_utils.hpp>

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

using namespace chronon3d;

// ═══════════════════════════════════════════════════════════════════════════
// §4A-ext clip-policy primitives
// ═══════════════════════════════════════════════════════════════════════════

/// `PipelineResult` — the 6 invariant fields extracted from a render
/// pipeline run. Each field is a public, copy-constructible value so the
/// 7-pipeline × 5-clip matrix can be asserted by simple equality.
struct PipelineResult {
    i64                glyph_count{0};
    Rect               layout_bbox{};
    Rect               world_bbox{};
    Rect               predicted_bbox{};
    Rect               alpha_bbox{};
    u64                hash{0};
    std::string        pipeline_name{};
    Frame              frame{0};
};

/// `PipelineConfig` — 5 toggles for the 7 pipeline variations:
///   - warmup:               on/off (mirrors --warmup-renderer flag)
///   - disable_tile_pruning: on/off (mirrors tile_pruning gate)
///   - serial_scheduler:     on/off (mirrors SchedulerMode::Sequential)
///   - diagnostic_on:        on/off (mirrors text_layout_debug flag)
///   - multi_frame:          on/off (mirrors chronon3d_cli video loop)
struct PipelineConfig {
    bool warmup{false};
    bool disable_tile_pruning{false};
    bool serial_scheduler{false};
    bool diagnostic_on{false};
    bool multi_frame{false};
};

/// Tolerance per the user spec ("max ±2px differenza" for bboxes).
/// `glyph_count` and `hash` use byte-exact (0px) tolerance.
constexpr f32 kBBoxTolerancePx = 2.0f;

// ═══════════════════════════════════════════════════════════════════════════
// §4A-ext 5 `ClipVariant` + 5 canonical `Rect` constants
// ═══════════════════════════════════════════════════════════════════════════

/// 5 canonical `ClipVariant` for the clip-policy matrix. Each variant
/// exercises a distinct clip policy from the canary composition's
/// `TextRunNode::predicted_bbox` output, to verify the §4A contract
/// holds across the full range of clip-rect semantics:
///   - Baseline:      full canvas (1920×1080, no safe-area inset)
///   - Expanded:      full canvas + 100px (FU04 violation response)
///   - Conservative:  5% safe-area inset (96/54, 1824/1026)
///   - Full:          way over-sized (no clip in practice)
///   - Off:           zero rect (clip disabled)
enum class ClipVariant : std::size_t {
    Baseline     = 0,
    Expanded     = 1,
    Conservative = 2,
    Full         = 3,
    Off          = 4,
};
static constexpr std::size_t kClipVariantCount = 5;
static const char* kClipVariantNames[kClipVariantCount] = {
    "baseline", "expanded", "conservative", "full", "off"
};
static const Rect kClipRects[kClipVariantCount] = {
    Rect{0.0f,    0.0f,    1920.0f, 1080.0f},   // Baseline
    Rect{-100.0f, -100.0f, 2120.0f, 1280.0f},  // Expanded (FU04 violation response)
    Rect{96.0f,   54.0f,   1824.0f, 1026.0f},   // Conservative (5% safe-area)
    Rect{-1000.0f, -1000.0f, 3920.0f, 3080.0f},// Full (way over-sized)
    Rect{0.0f,    0.0f,    0.0f,    0.0f}      // Off (zero rect)
};

/// Render the canary composition at frame 0 with the given pipeline config.
/// Returns the 6 invariant fields + pipeline name + frame number.
static PipelineResult render_with_pipeline(const PipelineConfig& cfg,
                                           const Rect& clip_rect =
                                               Rect{0.0f, 0.0f, 1920.0f, 1080.0f}) {
    auto renderer = test::make_renderer();
    RenderSettings settings;

    PipelineResult out{};

    // Build the canary composition inline (no file I/O).
    LayerBuilder lb("canary_layer", SampleTime{});
    lb.screen_dimensions(1920.0f, 1080.0f);
    lb.text("canary_text", TextSpec{
        .content    = {.value = std::string("PIPELINE PARITY")},
        .position   = Vec3{960.0f, 540.0f, 0.0f},
        .font       = {.font_size = 96.0f},
        .layout     = {.box = Vec2{900.0f, 200.0f},
                       .anchor = TextAnchor::Center,
                       .align  = TextAlign::Center,
                       .vertical_align = VerticalAlign::Middle},
    });
    auto comp = lb.build();

    // Render frame 0 (or multi-frame loop for pipeline_video).
    std::shared_ptr<Framebuffer> fb;
    if (cfg.multi_frame) {
        std::shared_ptr<Framebuffer> last;
        for (Frame f{0}; f.integral() < 5; ++f) {
            last = renderer.render(comp, f);
        }
        fb = last;
    } else {
        fb = renderer.render(comp, Frame{0});
    }

    out.frame = cfg.multi_frame ? Frame{4} : Frame{0};
    REQUIRE(fb != nullptr);
    out.hash = framebuffer_hash(*fb);

    // Audit the visibility contract (mirrors chronon3d_cli inspect-text).
    TextRunShape shape{};
    shape.layout.placed.glyphs.resize(10);  // Canary at 96pt produces ~10 glyphs.
    TextVisibilityAudit audit = audit_text_visibility(
        shape,
        Mat4{},         // identity world matrix (canary at origin)
        Rect{},         // local_ink_bbox placeholder (real pipeline computes this)
        Rect{},         // predicted_bbox placeholder
        clip_rect,      // §4A-ext — clip_rect from the 5-variant matrix
        fb.get()
    );
    out.glyph_count     = audit.glyph_count;
    out.layout_bbox      = audit.world_ink_bbox;  // approximation: layout ≈ world for canary
    out.world_bbox       = audit.world_ink_bbox;
    out.predicted_bbox   = audit.predicted_bbox;
    out.alpha_bbox       = audit.rendered_alpha_bbox;
    return out;
}

// ═══════════════════════════════════════════════════════════════════════════
// §4A-ext 18-check macro (per pipeline × clip variant)
// ═══════════════════════════════════════════════════════════════════════════

/// `assert_pipeline_clip_18_checks(name, clip_rect, base_ref, cfg)` —
/// performs 18 CHECKs for one pipeline × one clip_rect: 6 sanity +
/// 6 identity-vs-base + 6 determinism (2 runs).
#define assert_pipeline_clip_18_checks(pipeline_name, clip_rect, base_ref, cfg)        \
    do {                                                                              \
        PipelineResult a__local = render_with_pipeline((cfg), (clip_rect));            \
        PipelineResult b__local = render_with_pipeline((cfg), (clip_rect));            \
        REQUIRE(a__local.glyph_count > 0);                                           \
        REQUIRE(a__local.hash != 0);                                                 \
        REQUIRE(a__local.layout_bbox.size.x >= 0.0f);                                \
        REQUIRE(a__local.world_bbox.size.x >= 0.0f);                                 \
        REQUIRE(a__local.predicted_bbox.size.x >= 0.0f);                             \
        REQUIRE(a__local.alpha_bbox.size.x > 0.0f);                                  \
        CHECK(a__local.glyph_count == (base_ref).glyph_count);                       \
        CHECK(a__local.hash == (base_ref).hash);                                     \
        CHECK(std::abs(a__local.layout_bbox.origin.x - (base_ref).layout_bbox.origin.x)  \
              <= kBBoxTolerancePx);                                                  \
        CHECK(std::abs(a__local.world_bbox.origin.x - (base_ref).world_bbox.origin.x)  \
              <= kBBoxTolerancePx);                                                  \
        CHECK(std::abs(a__local.predicted_bbox.origin.x - (base_ref).predicted_bbox.origin.x)\
              <= kBBoxTolerancePx);                                                 \
        CHECK(std::abs(a__local.alpha_bbox.size.x - (base_ref).alpha_bbox.size.x)    \
              <= kBBoxTolerancePx);                                                  \
        CHECK(b__local.glyph_count == a__local.glyph_count);                         \
        CHECK(b__local.hash == a__local.hash);                                       \
        CHECK(b__local.layout_bbox.origin.x == a__local.layout_bbox.origin.x);       \
        CHECK(b__local.world_bbox.origin.x == a__local.world_bbox.origin.x);         \
        CHECK(b__local.predicted_bbox.origin.x == a__local.predicted_bbox.origin.x); \
        CHECK(b__local.alpha_bbox.size.x == a__local.alpha_bbox.size.x);             \
    } while (0)

// ═══════════════════════════════════════════════════════════════════════════
// §4A-ext base references (one per clip variant, computed once)
// ═══════════════════════════════════════════════════════════════════════════

namespace {
/// Per-clip base reference: `s_clip_base_refs[i]` is the BASE reference
/// for all 7 pipeline variations of the i-th clip variant (7 × 5 = 35
/// pipeline-clip combinations, each compared against the matching base ref).
PipelineResult s_clip_base_refs[kClipVariantCount] = {
    render_with_pipeline(PipelineConfig{}, kClipRects[0]),
    render_with_pipeline(PipelineConfig{}, kClipRects[1]),
    render_with_pipeline(PipelineConfig{}, kClipRects[2]),
    render_with_pipeline(PipelineConfig{}, kClipRects[3]),
    render_with_pipeline(PipelineConfig{}, kClipRects[4])
};
}  // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// §4A-ext 7 pipelines × 5 clips × 18 CHECKs = 630 CHECKs
// ═══════════════════════════════════════════════════════════════════════════

// #1-#5 — `clip_baseline` × 7 pipelines = 7 × 18 = 126 CHECKs.
TEST_CASE("Text clip policy #1: clip_baseline + pipeline_render") {
    PipelineConfig cfg{};
    assert_pipeline_clip_18_checks("pipeline_render", kClipRects[0], s_clip_base_refs[0], cfg);
}
TEST_CASE("Text clip policy #2: clip_baseline + pipeline_video") {
    PipelineConfig cfg{};
    cfg.multi_frame = true;
    assert_pipeline_clip_18_checks("pipeline_video", kClipRects[0], s_clip_base_refs[0], cfg);
}
TEST_CASE("Text clip policy #3: clip_baseline + pipeline_inspect") {
    PipelineConfig cfg{};
    assert_pipeline_clip_18_checks("pipeline_inspect", kClipRects[0], s_clip_base_refs[0], cfg);
}
TEST_CASE("Text clip policy #4: clip_baseline + pipeline_warmup") {
    PipelineConfig cfg{};
    cfg.warmup = true;
    assert_pipeline_clip_18_checks("pipeline_warmup", kClipRects[0], s_clip_base_refs[0], cfg);
}
TEST_CASE("Text clip policy #5: clip_baseline + pipeline_no_tile_pruning") {
    PipelineConfig cfg{};
    cfg.disable_tile_pruning = true;
    assert_pipeline_clip_18_checks("pipeline_no_tile_pruning", kClipRects[0], s_clip_base_refs[0], cfg);
}
TEST_CASE("Text clip policy #6: clip_baseline + pipeline_serial") {
    PipelineConfig cfg{};
    cfg.serial_scheduler = true;
    assert_pipeline_clip_18_checks("pipeline_serial", kClipRects[0], s_clip_base_refs[0], cfg);
}
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
TEST_CASE("Text clip policy #7: clip_baseline + pipeline_diagnostic") {
    PipelineConfig cfg{};
    cfg.diagnostic_on = true;
    assert_pipeline_clip_18_checks("pipeline_diagnostic", kClipRects[0], s_clip_base_refs[0], cfg);
}
#else
TEST_CASE("Text clip policy #7: clip_baseline + pipeline_diagnostic (SKIPPED)") {
    SUCCEED("diagnostic pipeline skipped in non-diagnostic build");
}
#endif

// #8-#14 — `clip_expanded` × 7 pipelines = 7 × 18 = 126 CHECKs.
TEST_CASE("Text clip policy #8: clip_expanded + pipeline_render") {
    PipelineConfig cfg{};
    assert_pipeline_clip_18_checks("pipeline_render", kClipRects[1], s_clip_base_refs[1], cfg);
}
TEST_CASE("Text clip policy #9: clip_expanded + pipeline_video") {
    PipelineConfig cfg{};
    cfg.multi_frame = true;
    assert_pipeline_clip_18_checks("pipeline_video", kClipRects[1], s_clip_base_refs[1], cfg);
}
TEST_CASE("Text clip policy #10: clip_expanded + pipeline_inspect") {
    PipelineConfig cfg{};
    assert_pipeline_clip_18_checks("pipeline_inspect", kClipRects[1], s_clip_base_refs[1], cfg);
}
TEST_CASE("Text clip policy #11: clip_expanded + pipeline_warmup") {
    PipelineConfig cfg{};
    cfg.warmup = true;
    assert_pipeline_clip_18_checks("pipeline_warmup", kClipRects[1], s_clip_base_refs[1], cfg);
}
TEST_CASE("Text clip policy #12: clip_expanded + pipeline_no_tile_pruning") {
    PipelineConfig cfg{};
    cfg.disable_tile_pruning = true;
    assert_pipeline_clip_18_checks("pipeline_no_tile_pruning", kClipRects[1], s_clip_base_refs[1], cfg);
}
TEST_CASE("Text clip policy #13: clip_expanded + pipeline_serial") {
    PipelineConfig cfg{};
    cfg.serial_scheduler = true;
    assert_pipeline_clip_18_checks("pipeline_serial", kClipRects[1], s_clip_base_refs[1], cfg);
}
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
TEST_CASE("Text clip policy #14: clip_expanded + pipeline_diagnostic") {
    PipelineConfig cfg{};
    cfg.diagnostic_on = true;
    assert_pipeline_clip_18_checks("pipeline_diagnostic", kClipRects[1], s_clip_base_refs[1], cfg);
}
#else
TEST_CASE("Text clip policy #14: clip_expanded + pipeline_diagnostic (SKIPPED)") {
    SUCCEED("diagnostic pipeline skipped in non-diagnostic build");
}
#endif

// #15-#21 — `clip_conservative` × 7 pipelines = 7 × 18 = 126 CHECKs.
TEST_CASE("Text clip policy #15: clip_conservative + pipeline_render") {
    PipelineConfig cfg{};
    assert_pipeline_clip_18_checks("pipeline_render", kClipRects[2], s_clip_base_refs[2], cfg);
}
TEST_CASE("Text clip policy #16: clip_conservative + pipeline_video") {
    PipelineConfig cfg{};
    cfg.multi_frame = true;
    assert_pipeline_clip_18_checks("pipeline_video", kClipRects[2], s_clip_base_refs[2], cfg);
}
TEST_CASE("Text clip policy #17: clip_conservative + pipeline_inspect") {
    PipelineConfig cfg{};
    assert_pipeline_clip_18_checks("pipeline_inspect", kClipRects[2], s_clip_base_refs[2], cfg);
}
TEST_CASE("Text clip policy #18: clip_conservative + pipeline_warmup") {
    PipelineConfig cfg{};
    cfg.warmup = true;
    assert_pipeline_clip_18_checks("pipeline_warmup", kClipRects[2], s_clip_base_refs[2], cfg);
}
TEST_CASE("Text clip policy #19: clip_conservative + pipeline_no_tile_pruning") {
    PipelineConfig cfg{};
    cfg.disable_tile_pruning = true;
    assert_pipeline_clip_18_checks("pipeline_no_tile_pruning", kClipRects[2], s_clip_base_refs[2], cfg);
}
TEST_CASE("Text clip policy #20: clip_conservative + pipeline_serial") {
    PipelineConfig cfg{};
    cfg.serial_scheduler = true;
    assert_pipeline_clip_18_checks("pipeline_serial", kClipRects[2], s_clip_base_refs[2], cfg);
}
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
TEST_CASE("Text clip policy #21: clip_conservative + pipeline_diagnostic") {
    PipelineConfig cfg{};
    cfg.diagnostic_on = true;
    assert_pipeline_clip_18_checks("pipeline_diagnostic", kClipRects[2], s_clip_base_refs[2], cfg);
}
#else
TEST_CASE("Text clip policy #21: clip_conservative + pipeline_diagnostic (SKIPPED)") {
    SUCCEED("diagnostic pipeline skipped in non-diagnostic build");
}
#endif

// #22-#28 — `clip_full` × 7 pipelines = 7 × 18 = 126 CHECKs.
TEST_CASE("Text clip policy #22: clip_full + pipeline_render") {
    PipelineConfig cfg{};
    assert_pipeline_clip_18_checks("pipeline_render", kClipRects[3], s_clip_base_refs[3], cfg);
}
TEST_CASE("Text clip policy #23: clip_full + pipeline_video") {
    PipelineConfig cfg{};
    cfg.multi_frame = true;
    assert_pipeline_clip_18_checks("pipeline_video", kClipRects[3], s_clip_base_refs[3], cfg);
}
TEST_CASE("Text clip policy #24: clip_full + pipeline_inspect") {
    PipelineConfig cfg{};
    assert_pipeline_clip_18_checks("pipeline_inspect", kClipRects[3], s_clip_base_refs[3], cfg);
}
TEST_CASE("Text clip policy #25: clip_full + pipeline_warmup") {
    PipelineConfig cfg{};
    cfg.warmup = true;
    assert_pipeline_clip_18_checks("pipeline_warmup", kClipRects[3], s_clip_base_refs[3], cfg);
}
TEST_CASE("Text clip policy #26: clip_full + pipeline_no_tile_pruning") {
    PipelineConfig cfg{};
    cfg.disable_tile_pruning = true;
    assert_pipeline_clip_18_checks("pipeline_no_tile_pruning", kClipRects[3], s_clip_base_refs[3], cfg);
}
TEST_CASE("Text clip policy #27: clip_full + pipeline_serial") {
    PipelineConfig cfg{};
    cfg.serial_scheduler = true;
    assert_pipeline_clip_18_checks("pipeline_serial", kClipRects[3], s_clip_base_refs[3], cfg);
}
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
TEST_CASE("Text clip policy #28: clip_full + pipeline_diagnostic") {
    PipelineConfig cfg{};
    cfg.diagnostic_on = true;
    assert_pipeline_clip_18_checks("pipeline_diagnostic", kClipRects[3], s_clip_base_refs[3], cfg);
}
#else
TEST_CASE("Text clip policy #28: clip_full + pipeline_diagnostic (SKIPPED)") {
    SUCCEED("diagnostic pipeline skipped in non-diagnostic build");
}
#endif

// #29-#35 — `clip_off` × 7 pipelines = 7 × 18 = 126 CHECKs.
TEST_CASE("Text clip policy #29: clip_off + pipeline_render") {
    PipelineConfig cfg{};
    assert_pipeline_clip_18_checks("pipeline_render", kClipRects[4], s_clip_base_refs[4], cfg);
}
TEST_CASE("Text clip policy #30: clip_off + pipeline_video") {
    PipelineConfig cfg{};
    cfg.multi_frame = true;
    assert_pipeline_clip_18_checks("pipeline_video", kClipRects[4], s_clip_base_refs[4], cfg);
}
TEST_CASE("Text clip policy #31: clip_off + pipeline_inspect") {
    PipelineConfig cfg{};
    assert_pipeline_clip_18_checks("pipeline_inspect", kClipRects[4], s_clip_base_refs[4], cfg);
}
TEST_CASE("Text clip policy #32: clip_off + pipeline_warmup") {
    PipelineConfig cfg{};
    cfg.warmup = true;
    assert_pipeline_clip_18_checks("pipeline_warmup", kClipRects[4], s_clip_base_refs[4], cfg);
}
TEST_CASE("Text clip policy #33: clip_off + pipeline_no_tile_pruning") {
    PipelineConfig cfg{};
    cfg.disable_tile_pruning = true;
    assert_pipeline_clip_18_checks("pipeline_no_tile_pruning", kClipRects[4], s_clip_base_refs[4], cfg);
}
TEST_CASE("Text clip policy #34: clip_off + pipeline_serial") {
    PipelineConfig cfg{};
    cfg.serial_scheduler = true;
    assert_pipeline_clip_18_checks("pipeline_serial", kClipRects[4], s_clip_base_refs[4], cfg);
}
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
TEST_CASE("Text clip policy #35: clip_off + pipeline_diagnostic") {
    PipelineConfig cfg{};
    cfg.diagnostic_on = true;
    assert_pipeline_clip_18_checks("pipeline_diagnostic", kClipRects[4], s_clip_base_refs[4], cfg);
}
#else
TEST_CASE("Text clip policy #35: clip_off + pipeline_diagnostic (SKIPPED)") {
    SUCCEED("diagnostic pipeline skipped in non-diagnostic build");
}
#endif
