// ============================================================================
// tests/render_graph/nodes/test_text_run_predicted_bbox.cpp
//
// BUG 1 / TICKET-TEXT-XOFFSET-DOUBLE — Option A regression lock.
//
// Step 2 empirical confirmation (per docs/tickets/TICKET-XOFFSET-ROOT-CAUSE.md):
// canvas-centered text appeared at x ≈ 302 (658 px left of 960 on a 1920×1080
// canvas).  The 658 px left-shift was traced to the TICKET-104 strip+replay path
// in `src/render_graph/builder/graph_builder_coordinates.hpp` ~line 100: when
// `is_implicit_2d_centering_only` returned true for Text-kind layers, the
// source pass stripped the implicit canvas-center with respect to the text
// box half-width on X, then the downstream node re-multiplied the result,
// double-shifting by the box half-width.
//
// OPTION A FIX (the production change): a 4-line `if (item.layer->
// kind == LayerKind::Text) return false;` early-return in
// `is_implicit_2d_centering_only` — ALREADY committed in main as commit
// `8f19d02c`.  This file is the regression lock: it renders a small
// centred TextRun composition (self-contained inline `AnimCertTitle`
// pattern, distinct from the canonical registered `CertTitle` to keep
// the test isolated from content-registry churn) at frame 0, then asserts
// the rasterised ink bounding-box centre X is within ±10 px of 960.
//
// NOTE: this test follows the render-framefbuffer + pixel-scan-alpha-bbox
// pattern of `tests/certification/test_cert_text_bbox.cpp:55-67` (the
// canonical CertTest suite that the prior `test(cert):` Step-2 commit
// uses for `CertTitle` / `CertLowerThird`).  The two tests are
// intentionally independent: the cert test locks the canonical cert
// composition, this test locks the Option A fix in isolation.  If
// either regresses to ~302 px, the corresponding ticket re-opens.
//
// AGENTS.md v0.1 cat-3 (silent-fallback detection) compliant: the test
// MUST fail (CHECK fails) when the Option A fix regresses; an empty
// bbox would also fail because we `CHECK_FALSE(bbox.empty())` before
// the centre assertion.
// ============================================================================

#include <doctest/doctest.h>

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>

#include <tests/helpers/test_utils.hpp>
#include <tests/text_golden/text_completeness/pixel_scan_helpers.hpp>

#include <algorithm>
#include <cmath>
#include <string>

using namespace chronon3d;

namespace {

// ── Self-contained AnimCertTitle-like composition ───────────────────────
// Inline, NOT registered in any CompositionRegistry.  Mirrors the
// intent of the canonical `CertTitle` (content/certification/cert_title.cpp:58)
// but is intentionally kept self-isolated from content-registry refactors
// per AGENTS.md "Fare test minimali" rule.  Reuses the canonical font
// asset path resolution via `tests/helpers/test_utils.hpp::make_renderer()`.
Composition build_anim_cert_title_comp(SoftwareRenderer& renderer) {
    return composition(
        {.name = "AnimCertTitle/bbox_test", .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1}, .duration = 1},
        [&renderer](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("title", [&renderer](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.pin_to(Anchor::Center);
                l.text_run("title_text", TextRunSpec{
                    .text = TextSpec{.content = {.value = "EPIC TITLE"}, .placement = TextPlacement{TextPlacementKind::Absolute,
                                                     {960.0f, 540.0f}}, .font = {.font_path   = "assets/fonts/Inter-Bold.ttf",
                                       .font_family = "Inter",
                                       .font_weight = 700,
                                       .font_size   = 120.0f}, .layout = {.box            = {1920.0f, 1080.0f},
                                       .align          = TextAlign::Center,
                                       .vertical_align = VerticalAlign::Middle}, .appearance = {.color = Color{1.0f, 1.0f, 1.0f, 1.0f}}},
                });
            });
            return s.build();
        });
}

} // anonymous namespace

// ── Regression lock ────────────────────────────────────────────────────
// The Option A fix releases the TICKET-104 strip for Text-kind only
// (see graph_builder_coordinates.hpp:88-92 BUG 1 citation block).
// If a future commit re-merges the strip+replay path for Text-kind
// without a corresponding fix, the rasterised text bbox centre X pulls
// back ≈ 658 px to ~302 (the original BUG 1 symptom).  This test catches
// that regression at ±10 px slack (tighter than the sign-defined 658 px
// baseline, looser than the prior CertTitle cert-test's ±2 px to absorb
// font-rasterisation rounding + anti-aliasing).
TEST_CASE("AnimCertTitle bbox centre X is within ±10 px of 960 (BUG 1 Option A lock)") {
    auto renderer = test::make_renderer();
    auto comp = build_anim_cert_title_comp(renderer);

    // Render at frame 0 (production render path; matches the
    // predicted_bbox semantics that TICKET-XOFFSET-ROOT-CAUSE.md
    // established are equal between predicted_bbox() and rendered
    // world_bbox — same build_world_matrix call drives both).
    auto fb = renderer.render(comp, Frame{0});
    REQUIRE(static_cast<bool>(fb));

    // Alpha-threshold ink bbox scan (matches `tests/certification/
    // test_cert_text_bbox.cpp:135` CHECK pattern).
    const auto bbox = completeness::alpha_bbox(*fb);

    INFO("AnimCertTitle bbox: x0=", bbox.x0, " y0=", bbox.y0,
         " x1=", bbox.x1, " y1=", bbox.y1);
    CHECK_FALSE(bbox.empty());

    // Bug-1 regression assertion: X bbox centre MUST be 960 ± 10 px
    // on a 1920×1080 canvas.  Without the Option A fix this would be
    // ~302 (= 960 − 658 = the documented X-shift delta).
    const float cx = (bbox.x0 + bbox.x1) * 0.5f;
    CHECK(std::abs(cx - 960.0f) <= 10.0f);

    // Belt-and-braces Y check for cross-axis corroboration (the BUG 1
    // symptom is X-only per the root-cause ADR; Y should remain
    // within normal canvas-centre tolerance).  Looser tolerance
    // because glyph ascender/descender geometry is height-dependent.
    const float cy = (bbox.y0 + bbox.y1) * 0.5f;
    CHECK(std::abs(cy - 540.0f) <= 30.0f);
}
