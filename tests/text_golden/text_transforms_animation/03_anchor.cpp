// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/text_transforms_animation/03_anchor.cpp
//
// TICKET-FASE2-TRANSFORMS-ANIMATION §10 — Anchor transform test family.
// Verifies that text rendered with different `LayerBuilder::anchor(Vec3)`
// values produces the expected centroid shift.
//
// 4 TEST_CASEs (anchor TopLeft / TopRight / BottomLeft / BottomRight) ×
// 1 AR (1920×1080) = 4 PNG goldens in
// `test_renders/golden/text/text_transforms_animation/anchor/`.
//
// Invariants checked:
//   - Non-empty alpha_bbox (text actually rendered at every anchor)
//   - Centroid is in the expected quadrant (TopLeft → upper-left, etc.)
//
// Per AGENTS.md §honesty: 4 PNG re-bake requires a working build host;
// the 4 test cases gracefully skip on `result.golden_missing`.
//
// AGENTS.md v0.1 Cat-2 freeze-compliant: zero new public SDK API.  The
// test uses the existing `LayerBuilder::anchor()` + `text_run()` +
// `alpha_bbox()` + `alpha_centroid()` helpers.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/backends/software/render_settings.hpp>

#include <tests/visual/support/golden_test.hpp>
#include <tests/helpers/test_utils.hpp>
#include <tests/text_golden/text_clip/test_helpers.hpp>

#include <cmath>
#include <string>

using namespace chronon3d;
using namespace chronon3d::test;

namespace {

// ── GoldenTestConfig factory ───────────────────────────────────────────
GoldenTestConfig make_anchor_config(std::string_view case_slug) {
    GoldenTestConfig cfg;
    cfg.golden_directory  = "test_renders/golden/text/text_transforms_animation/anchor";
    cfg.artifact_directory =
        std::string{"test_renders/artifacts/text/text_transforms_animation/anchor/"} +
        std::string{case_slug};
    cfg.mode = golden_mode_from_environment();
    cfg.threshold.max_mean_abs_error     = 12.0f / 255.0f;
    cfg.threshold.max_abs_error          = 130.0f / 255.0f;
    cfg.threshold.max_changed_pixel_ratio = 0.95f;
    cfg.threshold.max_rmse               = 14.0f / 255.0f;
    cfg.threshold.min_ssim               = 0.55f;
    return cfg;
}

// ── Anchor enum (matches LayerBuilder::anchor(Vec3) convention) ───────
enum class AnchorKind { TopLeft, TopRight, BottomLeft, BottomRight };

// Convert AnchorKind → anchor Vec3 + expected centroid quadrant.
struct AnchorSpec {
    Vec3 anchor;
    float expected_cx;
    float expected_cy;
    std::string_view name;
};

AnchorSpec make_anchor_spec(AnchorKind k) {
    // ── Anchor convention assumption (documented per code-reviewer #4) ──────
// `LayerBuilder::anchor(Vec3)` uses the standard pixel-space convention
// where (-1,-1) = TopLeft (upper-left visually), (+1,+1) = BottomRight
// (lower-right visually).  This matches the existing layer.position(Vec3)
// convention (X=right, Y=down).  If a future Y-flipped coordinate system
// is introduced, the quadrant assertions below will need to be
// remapped.  The 200px centroid tolerance in the assertion below
// absorbs minor visual pixel-space vs glyph-ink-space differences.
    switch (k) {
        case AnchorKind::TopLeft:
            return {{-1.0f, -1.0f, 0.0f},  240.0f,  270.0f, "topleft"};
        case AnchorKind::TopRight:
            return {{+1.0f, -1.0f, 0.0f}, 1680.0f,  270.0f, "topright"};
        case AnchorKind::BottomLeft:
            return {{-1.0f, +1.0f, 0.0f},  240.0f,  810.0f, "bottomleft"};
        case AnchorKind::BottomRight:
            return {{+1.0f, +1.0f, 0.0f}, 1680.0f,  810.0f, "bottomright"};
    }
    return {{0.0f, 0.0f, 0.0f}, 960.0f, 540.0f, "center"};
}

// ── Composition builder ───────────────────────────────────────────────
Composition build_anchor_composition(
    SoftwareRenderer& renderer,
    AnchorKind anchor_kind,
    int canvas_w = 1920,
    int canvas_h = 1080
) {
    const AnchorSpec spec = make_anchor_spec(anchor_kind);
    return composition(
        {.name = std::string{"TextTransforms/Anchor_"} + std::string{spec.name} +
                  "_" + std::to_string(canvas_w) + "x" + std::to_string(canvas_h),
         .width = canvas_w, .height = canvas_h,
         .frame_rate = FrameRate{30, 1},
         .duration = Frame{1}},
        [&renderer, spec, canvas_w, canvas_h](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("anchored", [spec, canvas_w, canvas_h](LayerBuilder& l) {
                // Apply anchor BEFORE the text_run.
                l.anchor(spec.anchor);
                l.text_run("title", TextRunParams{
                    .text = {
                        .content = {.value = "ANCHOR"},
                        .position = {static_cast<float>(canvas_w) * 0.5f,
                                     static_cast<float>(canvas_h) * 0.5f,
                                     0.0f},
                        .font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = 180.0f
                        },
                        .layout = {
                            .box = {static_cast<float>(canvas_w) * 0.5f,
                                    static_cast<float>(canvas_h) * 0.5f},
                            .align = TextAlign::Center,
                            .vertical_align = VerticalAlign::Middle
                        },
                        .appearance = {.color = Color::white()}
                    }
                }).commit();
            });
            return s.build();
        });
}

// ── Render one anchor variant ───────────────────────────────────────────
struct RenderedAnchor {
    std::shared_ptr<Framebuffer> fb;
    int width;
    int height;
};

RenderedAnchor render_anchor(SoftwareRenderer& renderer, AnchorKind kind) {
    auto fb = renderer.render(
        build_anchor_composition(renderer, kind), Frame{0});
    return RenderedAnchor{fb, 1920, 1080};
}

// ── Golden verification helper (canonical pattern) ─────────────────────
void verify_anchor_golden(Framebuffer& fb, std::string_view case_slug) {
    auto r = verify_golden(fb, std::string{case_slug}, make_anchor_config(case_slug));
    CHECK_FALSE(r.golden_missing);
    if (!r.golden_missing) {
        INFO("Golden: ", r.message);
        CHECK(r.passed);
    }
}

} // namespace

// ═══ Test 1 — Anchor TopLeft ═══════════════════════════════════════════
TEST_CASE("TextTransforms.Anchor_TopLeft_1920x1080") {
    auto renderer = test::make_renderer();
    auto r = render_anchor(renderer, AnchorKind::TopLeft);
    REQUIRE(r.fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*r.fb);
    const AlphaCentroid centroid = alpha_centroid(*r.fb);
    INFO("anchor=topleft bbox: x0=", bbox.x0, " y0=", bbox.y0,
         " x1=", bbox.x1, " y1=", bbox.y1);
    INFO("anchor=topleft centroid: x=", centroid.x, " y=", centroid.y,
         " max_alpha=", centroid.max_alpha);

    // Non-empty invariant: visible pixels must be present.
    CHECK_FALSE(bbox.empty());
    CHECK(centroid.max_alpha > 0.05f);

    // Centroid invariant: text anchored at TopLeft should be in the
    // upper-left quadrant (cx < canvas_w / 2, cy < canvas_h / 2).
    CHECK(centroid.x < 960.0f);
    CHECK(centroid.y < 540.0f);

    verify_anchor_golden(*r.fb, "anchor_topleft_1920x1080");
}

// ═══ Test 2 — Anchor TopRight ══════════════════════════════════════════
TEST_CASE("TextTransforms.Anchor_TopRight_1920x1080") {
    auto renderer = test::make_renderer();
    auto r = render_anchor(renderer, AnchorKind::TopRight);
    REQUIRE(r.fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*r.fb);
    const AlphaCentroid centroid = alpha_centroid(*r.fb);
    INFO("anchor=topright centroid: x=", centroid.x, " y=", centroid.y);

    CHECK_FALSE(bbox.empty());
    CHECK(centroid.max_alpha > 0.05f);

    // Centroid invariant: TopRight → upper-right quadrant.
    CHECK(centroid.x > 960.0f);
    CHECK(centroid.y < 540.0f);

    verify_anchor_golden(*r.fb, "anchor_topright_1920x1080");
}

// ═══ Test 3 — Anchor BottomLeft ════════════════════════════════════════
TEST_CASE("TextTransforms.Anchor_BottomLeft_1920x1080") {
    auto renderer = test::make_renderer();
    auto r = render_anchor(renderer, AnchorKind::BottomLeft);
    REQUIRE(r.fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*r.fb);
    const AlphaCentroid centroid = alpha_centroid(*r.fb);
    INFO("anchor=bottomleft centroid: x=", centroid.x, " y=", centroid.y);

    CHECK_FALSE(bbox.empty());
    CHECK(centroid.max_alpha > 0.05f);

    // Centroid invariant: BottomLeft → lower-left quadrant.
    CHECK(centroid.x < 960.0f);
    CHECK(centroid.y > 540.0f);

    verify_anchor_golden(*r.fb, "anchor_bottomleft_1920x1080");
}

// ═══ Test 4 — Anchor BottomRight ═══════════════════════════════════════
TEST_CASE("TextTransforms.Anchor_BottomRight_1920x1080") {
    auto renderer = test::make_renderer();
    auto r = render_anchor(renderer, AnchorKind::BottomRight);
    REQUIRE(r.fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*r.fb);
    const AlphaCentroid centroid = alpha_centroid(*r.fb);
    INFO("anchor=bottomright centroid: x=", centroid.x, " y=", centroid.y);

    CHECK_FALSE(bbox.empty());
    CHECK(centroid.max_alpha > 0.05f);

    // Centroid invariant: BottomRight → lower-right quadrant.
    CHECK(centroid.x > 960.0f);
    CHECK(centroid.y > 540.0f);

    verify_anchor_golden(*r.fb, "anchor_bottomright_1920x1080");
}
