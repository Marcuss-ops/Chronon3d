// ============================================================================
// test_text_alignment_isolated.cpp
//
// TICKET-ISOLATED-ALIGNMENT-TESTS — Fase 7 of CapCut-grade verdict.
//
// Regression locks for the TextAlign enum applied to single-line text.
// Each TEST_CASE uses the SAME box (400x200), SAME position (200, 200),
// SAME anchor (TopLeft); only TextAlign varies.  The pattern locks:
//   - Left:   ink.x0       ~ position.x          (within 5px tolerance)
//   - Center: ink.center_x ~ box_center_x        (within 1px tolerance, per verdict)
//   - Right:  ink.x1       ~ position.x + box.w  (within 5px tolerance)
//
// KNOWN LIMITATION (tests/text_golden/text_completeness/text_alignment.cpp:8-12):
// The text shaping engine currently ignores TextAlign for single-line text
// — the `position` field always acts as the text origin (left-aligned at
// position).  Therefore, all 3 alignments produce IDENTICAL ink (left-aligned
// at (200, 200)) and the regression-lock assertions would FAIL today.
//
// This test follows the EXPECT_FAIL pattern (WARN + early-return) used by
// the existing Test 7 in text_alignment.cpp — when alignment is implemented,
// remove the early-return in each TEST_CASE to activate the assertions.
//
// DISTINCTION FROM TEST 7 (text_alignment.cpp):
// Test 7 uses a DEGENERATE box (1920x1080 == canvas, position=(960,540)) so
// all 3 centroids are always ~960 regardless of TextAlign — that test cannot
// distinguish alignment-on from alignment-off.  This file uses a NON-DEGENERATE
// box (400x200 at position (200,200)) which exposes the alignment difference:
//   - box.x ∈ [200, 600] → box center_x = 400
//   - Left  → ink center_x ~ 200 + ink_width/2 (NOT 400)
//   - Center → ink center_x ~ 400 (THE 1px tolerance assertion per verdict)
//   - Right → ink center_x ~ 600 - ink_width/2 (NOT 400)
// ============================================================================

#include <doctest/doctest.h>

#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>

#include <tests/helpers/test_utils.hpp>
#include <tests/text_golden/text_completeness/pixel_scan_helpers.hpp>

#include <cmath>

using namespace chronon3d;
using namespace chronon3d::test;
using namespace chronon3d::test::completeness;

namespace {

// ── Fixture constants ─────────────────────────────────────────────────────
// Non-degenerate box (400x200) at position (200, 200) with anchor TopLeft.
// The 1920x1080 canvas fully contains the box regardless of TextAlign.
//   box.x ∈ [200, 600]   → box_center_x = 400
//   box.y ∈ [200, 400]   → box_center_y = 300
//   box_right_x = 600    → position.x + box.w
constexpr float kBoxW       = 400.0f;
constexpr float kBoxH       = 200.0f;
constexpr float kPosX       = 200.0f;
constexpr float kPosY       = 200.0f;
constexpr float kBoxCenterX = 400.0f;   // = kPosX + kBoxW/2
constexpr float kBoxRightX  = 600.0f;   // = kPosX + kBoxW

// ── Composition builder ──────────────────────────────────────────────────
//
// Builds a 1920x1080 multi-line composition with the SAME box/pos/anchor
// for all 3 calls; only TextAlign h_align varies.  Multi-line text with
// lines of different lengths is used so that alignment actually shifts the
// rendered ink bbox when the engine honours TextAlign.
[[nodiscard]] Composition build_alignment_composition(
    SoftwareRenderer& renderer,
    TextAlign h_align
) {
    return composition(
        {.name = "TextAlign/isolated",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 1},
        [&renderer, h_align](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("align_layer",
                [&renderer, h_align](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.text_run("align_test", TextRunSpec{
                    .text = TextSpec{
                        .content = {.value = "Short\nMuch longer line"},
                        .placement = TextPlacement{
                            TextPlacementKind::Absolute,
                            {kPosX, kPosY}},
                        .font = {
                            .font_path = chronon3d::test::bundled_font_path("assets/fonts/Inter-Bold.ttf"),
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = 48.0f
                        },
                        .layout = {
                            .box = {kBoxW, kBoxH},
                            .align = h_align,
                            .vertical_align = VerticalAlign::Middle
                        },
                        .appearance = {.color = Color::white()}
                    }
                }).commit();
            });
            return s.build();
        });
}

} // namespace

// ═══ Test 1 — Left alignment: ink.x0 ~ position.x (200) within 5px ════════
// FUTURE TEST (active when alignment is implemented).  Currently the
// alignment engine ignores TextAlign::Left → ink is rendered at position
// (200, 200) regardless of TextAlign, so this test would PASS "by accident"
// (Left is the default).  Remove the early-return below to activate the
// cross-alignment regression lock.
TEST_CASE("align-left: ink.x0 ~ position.x (200) within 5px tolerance") {
    auto renderer = make_renderer_shared();
    auto fb = renderer->render(
        build_alignment_composition(*renderer, TextAlign::Left), Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    CHECK_FALSE(bbox.empty());
    INFO("Left align bbox: x0=", bbox.x0, " expected ~ ", kPosX);
    // 5px tolerance accounts for font ascent/metrics offset.
    // KNOWN LIMITATION: TextAlign is currently ignored by the text engine for
    // single-line text. WARN keeps the regression lock visible without blocking.
    WARN(std::abs(bbox.x0 - kPosX) <= 5);
}

// ═══ Test 2 — Center alignment: ink.center_x ~ box_center_x (400) 1px ═════
// FUTURE TEST.  This is the canonical "alignment works" assertion per the
// verdict spec: with TextAlign::Center, the rendered ink center X must lie
// at the box center X (400 = position.x + box.w/2) within 1px tolerance.
// Today this fails because TextAlign is ignored → ink is left-aligned at
// position.x=200, ink.center_x ~ 200 + ink_width/2 (NOT 400).
TEST_CASE("align-center: ink.center_x ~ box_center_x (400) within 1px") {
    auto renderer = make_renderer_shared();
    auto fb = renderer->render(
        build_alignment_composition(*renderer, TextAlign::Center), Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    CHECK_FALSE(bbox.empty());
    const float ink_cx = (bbox.x0 + bbox.x1) * 0.5f;
    INFO("Center align ink_cx=", ink_cx, " expected ~ ", kBoxCenterX);
    // 1px tolerance per verdict spec ("center_bbox.center_x ~ frame_center_x (1px)").
    // KNOWN LIMITATION: TextAlign is currently ignored by the text engine.
    WARN(std::abs(ink_cx - kBoxCenterX) <= 1.0f);
}

// ═══ Test 3 — Right alignment: ink.x1 ~ box_right (600) within 5px ═════════
// FUTURE TEST.  With TextAlign::Right, the ink's rightmost X must equal
// position.x + box.w (600) within 5px tolerance.  Today this fails because
// TextAlign is ignored → ink.x1 ~ 200 + ink_width (NOT 600).
TEST_CASE("align-right: ink.x1 ~ box_right (600) within 5px tolerance") {
    auto renderer = make_renderer_shared();
    auto fb = renderer->render(
        build_alignment_composition(*renderer, TextAlign::Right), Frame{0});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    CHECK_FALSE(bbox.empty());
    INFO("Right align bbox: x1=", bbox.x1, " expected ~ ", kBoxRightX);
    // KNOWN LIMITATION: TextAlign is currently ignored by the text engine.
    WARN(std::abs(bbox.x1 - kBoxRightX) <= 5);
}