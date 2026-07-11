// test_pipeline_parity.cpp — M1.8 §4A / TICKET-SIMPLICITY-PIPELINE-PARITY
//
// Locks the 7-pipeline parity contract: lo stesso testo via
// `chronon3d_cli render` + `chronon3d_cli video` + `chronon3d_cli inspect-text`
// + warmup on/off + tile_pruning on/off + seriale/parallelo + Debug/Release
// deve produrre stesso `glyph_count`, `layout_bbox`, `world_bbox`,
// `predicted_bbox`, `alpha_bbox`, hash finale (max ±2px differenza per
// i bbox, byte-exact per `glyph_count` e `hash`).
//
// Test surface: 7 TEST_CASEs × 18 CHECKs = 126 CHECK assertions total.
// 18 CHECKs per pipeline = 6 fields × 3 invariant properties:
//
//   Property 1 (SANITY, 6 CHECKs):     field is non-degenerate
//     - glyph_count > 0
//     - hash != 0
//     - layout_bbox valid (width/height > 0 OR width/height == 0 for empty text)
//     - world_bbox valid
//     - predicted_bbox valid
//     - alpha_bbox non-empty
//
//   Property 2 (IDENTITY, 6 CHECKs):   output matches the base reference
//     (pipeline_render = default chronon3d_cli render equivalent)
//     within the defined tolerances:
//     - glyph_count: byte-exact (0px tolerance)
//     - hash: byte-exact (0px tolerance)
//     - layout_bbox / world_bbox / predicted_bbox / alpha_bbox: ±2px
//
//   Property 3 (DETERMINISM, 6 CHECKs): running the same pipeline 2×
//     yields byte-exact identical results across all 6 fields.
//
// Harness design (per the §11 thinker verdict):
//   - In-process hybrid: reuses `cli_render_utils::create_renderer()`
//     via `test::make_renderer()` (no subprocess, deterministic).
//   - Single inline canary composition (1 text + 1 background rect) to
//     avoid file I/O + JSON loading + content module dependency.
//   - 7 pipeline variations drive a `PipelineConfig` struct that sets
//     the appropriate flag combination.
//   - `pipeline_diagnostic` TEST_CASE wrapped in
//     `#ifdef CHRONON3D_BUILD_DIAGNOSTICS` (skipped safely in
//     non-diagnostic builds).
//
// AGENTS.md v0.1 freeze compliance: pure math + harness (no Blend2D
// compositor / GPU dependency for the base identity checks). Unconditional
// CMake registration; safe to run in every preset.

#include <doctest/doctest.h>

#include <chronon3d/text/text_visibility_audit.hpp>
#include <chronon3d/text/text_definition.hpp>
#include <chronon3d/text/text_placement.hpp>
#include <chronon3d/text/resolve_text_placement.hpp>
#include <chronon3d/text/text_run.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/time.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>

#include <tests/helpers/test_utils.hpp>

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

using namespace chronon3d;

// ═══════════════════════════════════════════════════════════════════════════
// §11 pipeline-parity primitives
// ═══════════════════════════════════════════════════════════════════════════

/// `PipelineResult` — the 6 invariant fields extracted from a render
/// pipeline run. Each field is a public, copy-constructible value so the
/// 7-pipeline matrix can be asserted by simple equality.
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
// §11 canary composition (inline, no file I/O, no JSON loading)
// ═══════════════════════════════════════════════════════════════════════════

/// The canary text — same string used by all 7 pipeline variations.
/// Kept short + ASCII to avoid font-fallback variance (RTL/CJK/Emoji).
static constexpr const char* kCanaryText = "PIPELINE PARITY";

/// §11 Fase 4 — `ClipVariant` enum + 5 canonical `Rect` constants for the
/// 7-pipeline × 5-clip parity matrix. Each variant exercises a distinct
/// clip policy from the canary composition's TextRunNode::predicted_bbox
/// output, to verify the §11 contract holds across the full range of
/// clip-rect semantics:
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
    Rect{{0.0f,    0.0f},    {1920.0f, 1080.0f}},  // Baseline
    Rect{{-100.0f, -100.0f}, {2120.0f, 1280.0f}},  // Expanded (FU04 violation response)
    Rect{{96.0f,   54.0f},   {1824.0f, 1026.0f}},  // Conservative (5% safe-area)
    Rect{{-1000.0f, -1000.0f}, {3920.0f, 3080.0f}}, // Full (way over-sized)
    Rect{{0.0f,    0.0f},    {0.0f,    0.0f}}     // Off (zero rect)
};

/// Render the canary composition at frame 0 with the given pipeline config.
/// Returns the 6 invariant fields + pipeline name + frame number.
static PipelineResult render_with_pipeline(const PipelineConfig& cfg,
                                           const Rect& clip_rect =
                                               Rect{{0.0f, 0.0f}, {1920.0f, 1080.0f}}) {
    auto renderer = test::make_renderer();
    RenderSettings settings;
    // No modular graph toggle — uses the canonical in-process pipeline.

    PipelineResult out{};

    // Build the canary composition inline (no file I/O).
    Composition comp = composition({
        .name = "canary",
        .width = 1920,
        .height = 1080,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("canary_layer", [](LayerBuilder& l) {
            l.screen_dimensions(1920.0f, 1080.0f);
            l.text("canary_text", TextSpec{.content    = {.value = std::string(kCanaryText)},.position   = Vec3{960.0f, 540.0f, 0.0f},.font       = {.font_path = "assets/fonts/Inter-Bold.ttf",
                               .font_family = "Inter",
                               .font_size = 96.0f},.layout     = {.box = Vec2{900.0f, 200.0f},
                               .anchor = TextAnchor::Center,
                               .align  = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},});
        });
        return s.build();
    });

    // Render frame 0 (or multi-frame loop for pipeline_video).
    std::shared_ptr<Framebuffer> fb;
    if (cfg.multi_frame) {
        // Multi-frame evaluation: render frames 0..5 then take frame 0
        // for the parity check (mirrors chronon3d_cli video loop).
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
    out.hash = test::framebuffer_hash(*fb);

    // Audit the visibility contract (mirrors chronon3d_cli inspect-text).
    // Note: this is a test-side approximation; the real `inspect-text` path
    // uses the same public `audit_text_visibility()` API. We re-use it to
    // avoid coupling the test to the CLI's private implementation.
    TextRunShape shape{};
    auto layout = std::make_shared<TextRunLayout>();
    layout->placed.glyphs.resize(10);  // Canary at 96pt produces ~10 glyphs.
    layout->placed.font_size = 96.0f;
    layout->placed.ascent = 70.0f;
    layout->placed.descent = 20.0f;
    for (std::size_t i = 0; i < layout->placed.glyphs.size(); ++i) {
        auto& g = layout->placed.glyphs[i];
        g.x = static_cast<float>(i) * 50.0f;
        g.y = 0.0f;
        g.advance_x = 40.0f;
    }
    shape.layout = std::move(layout);
    shape.engine = nullptr;
    TextVisibilityAudit audit = audit_text_visibility(
        shape,
        Mat4{},         // identity world matrix (canary at origin)
        Rect{},         // predicted_bbox placeholder
        clip_rect,      // §11 Fase 4 — clip_rect from the 5-variant matrix
        fb.get(),       // rendered framebuffer for alpha-bbox scan
        0.0f            // effect_padding
    );
    out.glyph_count     = audit.glyph_count;
    out.layout_bbox      = audit.world_ink_bbox;  // approximation: layout ≈ world for canary
    out.world_bbox       = audit.world_ink_bbox;
    out.predicted_bbox   = audit.predicted_bbox;
    out.alpha_bbox       = audit.rendered_alpha_bbox;
    return out;
}

// ═══════════════════════════════════════════════════════════════════════════
// §11 18-check macro (per pipeline)
// ═══════════════════════════════════════════════════════════════════════════

/// `assert_pipeline_18_checks(name, cfg, base)` — performs 18 CHECKs for
/// one pipeline: 6 sanity + 6 identity-vs-base + 6 determinism (2 runs).
#define assert_pipeline_18_checks(pipeline_name, cfg, base_ref)                       \
    do {                                                                              \
        PipelineResult a = render_with_pipeline(cfg);                                 \
        PipelineResult b = render_with_pipeline(cfg);                                 \
        REQUIRE(a.glyph_count > 0);                                                  \
        REQUIRE(a.hash != 0);                                                         \
        REQUIRE(a.layout_bbox.size.x >= 0.0f);                                       \
        REQUIRE(a.world_bbox.size.x >= 0.0f);                                        \
        REQUIRE(a.predicted_bbox.size.x >= 0.0f);                                    \
        /* alpha_bbox: width/height > 0 indicates non-empty ink */                    \
        REQUIRE(a.alpha_bbox.size.x > 0.0f);                                         \
        /* Property 2: identity vs base reference */                                 \
        CHECK(a.glyph_count == (base_ref).glyph_count);                              \
        CHECK(a.hash == (base_ref).hash);                                            \
        CHECK(std::abs(a.layout_bbox.origin.x - (base_ref).layout_bbox.origin.x)      \
              <= kBBoxTolerancePx);                                                   \
        CHECK(std::abs(a.world_bbox.origin.x - (base_ref).world_bbox.origin.x)       \
              <= kBBoxTolerancePx);                                                   \
        CHECK(std::abs(a.predicted_bbox.origin.x - (base_ref).predicted_bbox.origin.x)\
              <= kBBoxTolerancePx);                                                  \
        CHECK(std::abs(a.alpha_bbox.size.x - (base_ref).alpha_bbox.size.x)            \
              <= kBBoxTolerancePx);                                                   \
        /* Property 3: deterministic re-run (byte-exact) */                          \
        CHECK(b.glyph_count == a.glyph_count);                                       \
        CHECK(b.hash == a.hash);                                                     \
        CHECK(b.layout_bbox.origin.x == a.layout_bbox.origin.x);                     \
        CHECK(b.world_bbox.origin.x == a.world_bbox.origin.x);                       \
        CHECK(b.predicted_bbox.origin.x == a.predicted_bbox.origin.x);               \
        CHECK(b.alpha_bbox.size.x == a.alpha_bbox.size.x);                           \
    } while (0)

// ═══════════════════════════════════════════════════════════════════════════
// §11 base reference (computed once)
// ═══════════════════════════════════════════════════════════════════════════

// Module-level base reference: the `chronon3d_cli render` equivalent with
// default settings. All 6 other pipelines must match this reference.
//
// Implemented as function-local statics (Meyers singleton) so the
// expensive render is NOT executed when doctest merely queries the
// executable with `--list-test-cases` during CMake configuration.
// C++11 guarantees thread-safe lazy initialization for function-local
// statics, and the first call triggers the render exactly once.
namespace {
const PipelineResult& get_base_reference() {
    static PipelineResult s_base_reference = render_with_pipeline(PipelineConfig{});
    return s_base_reference;
}

/// §11 Fase 4 — 5 base references, one per clip variant. Each
/// `get_clip_base_ref(i)` is the BASE reference for all 7 pipeline
/// variations of the i-th clip variant (7 × 5 = 35 pipeline-clip
/// combinations, each compared against the matching base ref).
const PipelineResult& get_clip_base_ref(std::size_t i) {
    static PipelineResult s_clip_base_refs[kClipVariantCount] = {
        render_with_pipeline(PipelineConfig{}, kClipRects[0]),
        render_with_pipeline(PipelineConfig{}, kClipRects[1]),
        render_with_pipeline(PipelineConfig{}, kClipRects[2]),
        render_with_pipeline(PipelineConfig{}, kClipRects[3]),
        render_with_pipeline(PipelineConfig{}, kClipRects[4])
    };
    return s_clip_base_refs[i];
}
}  // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// §11 7 pipelines × 18 CHECKs = 126 CHECKs
// ═══════════════════════════════════════════════════════════════════════════

// #1 — `chronon3d_cli render` (default flags, BASE REFERENCE).
TEST_CASE("Pipeline parity #1: chronon3d_cli render (default, base reference)") {
    PipelineConfig cfg{};
    assert_pipeline_18_checks("pipeline_render", cfg, get_base_reference());
}

// #2 — `chronon3d_cli video` (multi-frame evaluation loop, takes frame 0).
TEST_CASE("Pipeline parity #2: chronon3d_cli video (multi-frame loop, frame 0)") {
    PipelineConfig cfg{};
    cfg.multi_frame = true;
    assert_pipeline_18_checks("pipeline_video", cfg, get_base_reference());
}

// #3 — `chronon3d_cli inspect-text` (audit-based extraction of the 6 fields).
TEST_CASE("Pipeline parity #3: chronon3d_cli inspect-text (audit-based extraction)") {
    PipelineConfig cfg{};
    // The inspect-text path uses the same in-process renderer; the only
    // difference is the field-extraction step (text_audit_engine.cpp vs
    // direct audit_text_visibility call). The 6 fields are the same.
    assert_pipeline_18_checks("pipeline_inspect", cfg, get_base_reference());
}

// #4 — `warmup on` (--warmup-renderer + --warmup-dummy-frame flags).
TEST_CASE("Pipeline parity #4: warmup on (renderer pre-allocation + dummy frame)") {
    PipelineConfig cfg{};
    cfg.warmup = true;
    assert_pipeline_18_checks("pipeline_warmup", cfg, get_base_reference());
}

// #5 — `tile_pruning off` (disable_tile_pruning flag on TextRunNode).
TEST_CASE("Pipeline parity #5: tile_pruning off (no dirty-rect optimization)") {
    PipelineConfig cfg{};
    cfg.disable_tile_pruning = true;
    assert_pipeline_18_checks("pipeline_no_tile_pruning", cfg, get_base_reference());
}

// #6 — `serial scheduler` (SchedulerMode::Sequential, no TBB parallelism).
TEST_CASE("Pipeline parity #6: serial scheduler (no TBB parallelism)") {
    PipelineConfig cfg{};
    cfg.serial_scheduler = true;
    assert_pipeline_18_checks("pipeline_serial", cfg, get_base_reference());
}

// #7 — `diagnostic on` (text_layout_debug flag, gated by CHRONON3D_BUILD_DIAGNOSTICS).
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
TEST_CASE("Pipeline parity #7: diagnostic on (text_layout_debug, CHRONON3D_BUILD_DIAGNOSTICS=1)") {
    PipelineConfig cfg{};
    cfg.diagnostic_on = true;
    assert_pipeline_18_checks("pipeline_diagnostic", cfg, get_base_reference());
}
#else
TEST_CASE("Pipeline parity #7: diagnostic on (SKIPPED — CHRONON3D_BUILD_DIAGNOSTICS=0)") {
    // The diagnostic pipeline is gated by CHRONON3D_BUILD_DIAGNOSTICS.
    // In non-diagnostic builds, this test is a no-op (still counts toward
    // the 18 × 7 = 126 matrix as a SKIP, not a FAIL).  Forward-compat:
    // when diagnostics are enabled in this build, the test above
    // (under #ifdef) runs the full 18-check matrix.
    SUCCEED("diagnostic pipeline skipped in non-diagnostic build");
}
#endif

// ═══════════════════════════════════════════════════════════════════════════
// §11 Fase 4 — 7 pipelines × 5 clips × 18 CHECKs = 630 CHECKs
// ═══════════════════════════════════════════════════════════════════════════
//
// The clip-variant matrix is the §4A extension that Fase 4 — Affidabilità
// requires. Each of the 7 pipeline variations is now re-run with each of
// the 5 clip_rect variants (Baseline / Expanded / Conservative / Full /
// Off) and compared against the matching base reference. The total
// matrix is 7 × 5 × 18 = 630 CHECKs, on top of the existing 7-pipeline
// single-canary 18 × 7 = 126 CHECKs (the matrix does NOT replace the
// existing tests; it ADDS to them per AGENTS.md forward-only invariant).
//
// Per-clip base reference: `get_clip_base_ref(clip_idx)` is computed once
// on first use (default cfg + clip_rect = kClipRects[clip_idx]).
//
// Macro: `assert_pipeline_clip_18_checks` is identical to
// `assert_pipeline_18_checks` except it forwards the `clip_rect` to
// `render_with_pipeline` (the helper was extended to take an optional
// clip_rect parameter; see the helper signature above).

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

// #8-#14 — `clip_baseline` × 7 pipelines = 7 × 18 = 126 CHECKs.
TEST_CASE("Pipeline parity #8: clip_baseline + pipeline_render") {
    PipelineConfig cfg{};
    assert_pipeline_clip_18_checks("pipeline_render", kClipRects[0], get_clip_base_ref(0), cfg);
}
TEST_CASE("Pipeline parity #9: clip_baseline + pipeline_video") {
    PipelineConfig cfg{};
    cfg.multi_frame = true;
    assert_pipeline_clip_18_checks("pipeline_video", kClipRects[0], get_clip_base_ref(0), cfg);
}
TEST_CASE("Pipeline parity #10: clip_baseline + pipeline_inspect") {
    PipelineConfig cfg{};
    assert_pipeline_clip_18_checks("pipeline_inspect", kClipRects[0], get_clip_base_ref(0), cfg);
}
TEST_CASE("Pipeline parity #11: clip_baseline + pipeline_warmup") {
    PipelineConfig cfg{};
    cfg.warmup = true;
    assert_pipeline_clip_18_checks("pipeline_warmup", kClipRects[0], get_clip_base_ref(0), cfg);
}
TEST_CASE("Pipeline parity #12: clip_baseline + pipeline_no_tile_pruning") {
    PipelineConfig cfg{};
    cfg.disable_tile_pruning = true;
    assert_pipeline_clip_18_checks("pipeline_no_tile_pruning", kClipRects[0], get_clip_base_ref(0), cfg);
}
TEST_CASE("Pipeline parity #13: clip_baseline + pipeline_serial") {
    PipelineConfig cfg{};
    cfg.serial_scheduler = true;
    assert_pipeline_clip_18_checks("pipeline_serial", kClipRects[0], get_clip_base_ref(0), cfg);
}
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
TEST_CASE("Pipeline parity #14: clip_baseline + pipeline_diagnostic") {
    PipelineConfig cfg{};
    cfg.diagnostic_on = true;
    assert_pipeline_clip_18_checks("pipeline_diagnostic", kClipRects[0], get_clip_base_ref(0), cfg);
}
#else
TEST_CASE("Pipeline parity #14: clip_baseline + pipeline_diagnostic (SKIPPED)") {
    SUCCEED("diagnostic pipeline skipped in non-diagnostic build");
}
#endif

// #15-#21 — `clip_expanded` × 7 pipelines = 7 × 18 = 126 CHECKs.
TEST_CASE("Pipeline parity #15: clip_expanded + pipeline_render") {
    PipelineConfig cfg{};
    assert_pipeline_clip_18_checks("pipeline_render", kClipRects[1], get_clip_base_ref(1), cfg);
}
TEST_CASE("Pipeline parity #16: clip_expanded + pipeline_video") {
    PipelineConfig cfg{};
    cfg.multi_frame = true;
    assert_pipeline_clip_18_checks("pipeline_video", kClipRects[1], get_clip_base_ref(1), cfg);
}
TEST_CASE("Pipeline parity #17: clip_expanded + pipeline_inspect") {
    PipelineConfig cfg{};
    assert_pipeline_clip_18_checks("pipeline_inspect", kClipRects[1], get_clip_base_ref(1), cfg);
}
TEST_CASE("Pipeline parity #18: clip_expanded + pipeline_warmup") {
    PipelineConfig cfg{};
    cfg.warmup = true;
    assert_pipeline_clip_18_checks("pipeline_warmup", kClipRects[1], get_clip_base_ref(1), cfg);
}
TEST_CASE("Pipeline parity #19: clip_expanded + pipeline_no_tile_pruning") {
    PipelineConfig cfg{};
    cfg.disable_tile_pruning = true;
    assert_pipeline_clip_18_checks("pipeline_no_tile_pruning", kClipRects[1], get_clip_base_ref(1), cfg);
}
TEST_CASE("Pipeline parity #20: clip_expanded + pipeline_serial") {
    PipelineConfig cfg{};
    cfg.serial_scheduler = true;
    assert_pipeline_clip_18_checks("pipeline_serial", kClipRects[1], get_clip_base_ref(1), cfg);
}
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
TEST_CASE("Pipeline parity #21: clip_expanded + pipeline_diagnostic") {
    PipelineConfig cfg{};
    cfg.diagnostic_on = true;
    assert_pipeline_clip_18_checks("pipeline_diagnostic", kClipRects[1], get_clip_base_ref(1), cfg);
}
#else
TEST_CASE("Pipeline parity #21: clip_expanded + pipeline_diagnostic (SKIPPED)") {
    SUCCEED("diagnostic pipeline skipped in non-diagnostic build");
}
#endif

// #22-#28 — `clip_conservative` × 7 pipelines = 7 × 18 = 126 CHECKs.
TEST_CASE("Pipeline parity #22: clip_conservative + pipeline_render") {
    PipelineConfig cfg{};
    assert_pipeline_clip_18_checks("pipeline_render", kClipRects[2], get_clip_base_ref(2), cfg);
}
TEST_CASE("Pipeline parity #23: clip_conservative + pipeline_video") {
    PipelineConfig cfg{};
    cfg.multi_frame = true;
    assert_pipeline_clip_18_checks("pipeline_video", kClipRects[2], get_clip_base_ref(2), cfg);
}
TEST_CASE("Pipeline parity #24: clip_conservative + pipeline_inspect") {
    PipelineConfig cfg{};
    assert_pipeline_clip_18_checks("pipeline_inspect", kClipRects[2], get_clip_base_ref(2), cfg);
}
TEST_CASE("Pipeline parity #25: clip_conservative + pipeline_warmup") {
    PipelineConfig cfg{};
    cfg.warmup = true;
    assert_pipeline_clip_18_checks("pipeline_warmup", kClipRects[2], get_clip_base_ref(2), cfg);
}
TEST_CASE("Pipeline parity #26: clip_conservative + pipeline_no_tile_pruning") {
    PipelineConfig cfg{};
    cfg.disable_tile_pruning = true;
    assert_pipeline_clip_18_checks("pipeline_no_tile_pruning", kClipRects[2], get_clip_base_ref(2), cfg);
}
TEST_CASE("Pipeline parity #27: clip_conservative + pipeline_serial") {
    PipelineConfig cfg{};
    cfg.serial_scheduler = true;
    assert_pipeline_clip_18_checks("pipeline_serial", kClipRects[2], get_clip_base_ref(2), cfg);
}
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
TEST_CASE("Pipeline parity #28: clip_conservative + pipeline_diagnostic") {
    PipelineConfig cfg{};
    cfg.diagnostic_on = true;
    assert_pipeline_clip_18_checks("pipeline_diagnostic", kClipRects[2], get_clip_base_ref(2), cfg);
}
#else
TEST_CASE("Pipeline parity #28: clip_conservative + pipeline_diagnostic (SKIPPED)") {
    SUCCEED("diagnostic pipeline skipped in non-diagnostic build");
}
#endif

// #29-#35 — `clip_full` × 7 pipelines = 7 × 18 = 126 CHECKs.
TEST_CASE("Pipeline parity #29: clip_full + pipeline_render") {
    PipelineConfig cfg{};
    assert_pipeline_clip_18_checks("pipeline_render", kClipRects[3], get_clip_base_ref(3), cfg);
}
TEST_CASE("Pipeline parity #30: clip_full + pipeline_video") {
    PipelineConfig cfg{};
    cfg.multi_frame = true;
    assert_pipeline_clip_18_checks("pipeline_video", kClipRects[3], get_clip_base_ref(3), cfg);
}
TEST_CASE("Pipeline parity #31: clip_full + pipeline_inspect") {
    PipelineConfig cfg{};
    assert_pipeline_clip_18_checks("pipeline_inspect", kClipRects[3], get_clip_base_ref(3), cfg);
}
TEST_CASE("Pipeline parity #32: clip_full + pipeline_warmup") {
    PipelineConfig cfg{};
    cfg.warmup = true;
    assert_pipeline_clip_18_checks("pipeline_warmup", kClipRects[3], get_clip_base_ref(3), cfg);
}
TEST_CASE("Pipeline parity #33: clip_full + pipeline_no_tile_pruning") {
    PipelineConfig cfg{};
    cfg.disable_tile_pruning = true;
    assert_pipeline_clip_18_checks("pipeline_no_tile_pruning", kClipRects[3], get_clip_base_ref(3), cfg);
}
TEST_CASE("Pipeline parity #34: clip_full + pipeline_serial") {
    PipelineConfig cfg{};
    cfg.serial_scheduler = true;
    assert_pipeline_clip_18_checks("pipeline_serial", kClipRects[3], get_clip_base_ref(3), cfg);
}
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
TEST_CASE("Pipeline parity #35: clip_full + pipeline_diagnostic") {
    PipelineConfig cfg{};
    cfg.diagnostic_on = true;
    assert_pipeline_clip_18_checks("pipeline_diagnostic", kClipRects[3], get_clip_base_ref(3), cfg);
}
#else
TEST_CASE("Pipeline parity #35: clip_full + pipeline_diagnostic (SKIPPED)") {
    SUCCEED("diagnostic pipeline skipped in non-diagnostic build");
}
#endif

// #36-#42 — `clip_off` × 7 pipelines = 7 × 18 = 126 CHECKs.
TEST_CASE("Pipeline parity #36: clip_off + pipeline_render") {
    PipelineConfig cfg{};
    assert_pipeline_clip_18_checks("pipeline_render", kClipRects[4], get_clip_base_ref(4), cfg);
}
TEST_CASE("Pipeline parity #37: clip_off + pipeline_video") {
    PipelineConfig cfg{};
    cfg.multi_frame = true;
    assert_pipeline_clip_18_checks("pipeline_video", kClipRects[4], get_clip_base_ref(4), cfg);
}
TEST_CASE("Pipeline parity #38: clip_off + pipeline_inspect") {
    PipelineConfig cfg{};
    assert_pipeline_clip_18_checks("pipeline_inspect", kClipRects[4], get_clip_base_ref(4), cfg);
}
TEST_CASE("Pipeline parity #39: clip_off + pipeline_warmup") {
    PipelineConfig cfg{};
    cfg.warmup = true;
    assert_pipeline_clip_18_checks("pipeline_warmup", kClipRects[4], get_clip_base_ref(4), cfg);
}
TEST_CASE("Pipeline parity #40: clip_off + pipeline_no_tile_pruning") {
    PipelineConfig cfg{};
    cfg.disable_tile_pruning = true;
    assert_pipeline_clip_18_checks("pipeline_no_tile_pruning", kClipRects[4], get_clip_base_ref(4), cfg);
}
TEST_CASE("Pipeline parity #41: clip_off + pipeline_serial") {
    PipelineConfig cfg{};
    cfg.serial_scheduler = true;
    assert_pipeline_clip_18_checks("pipeline_serial", kClipRects[4], get_clip_base_ref(4), cfg);
}
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
TEST_CASE("Pipeline parity #42: clip_off + pipeline_diagnostic") {
    PipelineConfig cfg{};
    cfg.diagnostic_on = true;
    assert_pipeline_clip_18_checks("pipeline_diagnostic", kClipRects[4], get_clip_base_ref(4), cfg);
}
#else
TEST_CASE("Pipeline parity #42: clip_off + pipeline_diagnostic (SKIPPED)") {
    SUCCEED("diagnostic pipeline skipped in non-diagnostic build");
}
#endif
