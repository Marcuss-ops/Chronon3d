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

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/backends/software/renderer.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>

#include <tests/helpers/test_utils.hpp>
#include <tests/support/pixel_scan_helpers.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <chronon3d/text/text_definition.hpp>
#include <chronon3d/text/resolve_text_placement.hpp>
#include <chronon3d/presets/text/text_presets_v1.hpp>

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
                l.text_run("title_text", PreparedText{
                    .document = {.utf8 = "EPIC TITLE"},
                    .style = {.font = {
                        .font_path = "assets/fonts/Inter-Bold.ttf",
                        .font_family = "Inter",
                        .font_weight = 700,
                        .font_size = 120.0f
                    }},
                    .frame = {.size = {1920.0f, 1080.0f},
                              .align = TextAlign::Center,
                              .vertical_align = VerticalAlign::Middle}
                });
            });
            return s.build();
        });
}

// Reproduces the exact production-v1 failure mode where a normal layer carries
// the implicit canvas-center while the TextRun carries a box-local
// T(-width/2,-height/2) anchor.  The parent and node transforms must be
// composed exactly once.  Stripping the parent center leaves the negative
// anchor uncompensated and produces a fully transparent framebuffer.
Composition build_full_canvas_visible_comp(SoftwareRenderer& renderer,
                                           int width,
                                           int height) {
    const float box_width = static_cast<float>(width);
    const float box_height = static_cast<float>(height);

    return composition(
        {.name = "TextRun/full_canvas_visible",
         .width = width,
         .height = height,
         .frame_rate = FrameRate{30, 1},
         .duration = 1},
        [&renderer, box_width, box_height](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("text_layer", [&renderer, box_width, box_height](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.text_run("text_run", PreparedText{
                    .document = {.utf8 = "VISIBLE INK"},
                    .style = {.font = {
                        .font_path = "assets/fonts/Inter-Bold.ttf",
                        .font_family = "Inter",
                        .font_weight = 700,
                        .font_size = 96.0f
                    }},
                    .frame = {.size = {box_width, box_height},
                              .placement = TextPlacement{TextPlacementKind::CanvasCenter,
                                                          {0.0f, 0.0f}},
                              .anchor = TextAnchor::Center,
                              .align = TextAlign::Center,
                              .vertical_align = VerticalAlign::Middle,
                              .wrap = TextWrap::Word,
                              .overflow = TextOverflow::Clip}
                }).commit();
            });
            return s.build();
        });
}

Composition build_pinned_absolute_animation_comp(SoftwareRenderer& renderer) {
    return composition(
        {.name = "TextRun/pinned_absolute_animation_boundary",
         .width = 1920,
         .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("name", [&renderer](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.pin_to(Anchor::Center);
                l.position_anim()
                    .key(Frame{0}, Vec3{0.0f, 30.0f, 0.0f},
                         EasingCurve{Easing::OutCubic})
                    .key(Frame{22}, Vec3{0.0f, 0.0f, 0.0f},
                         EasingCurve{Easing::OutCubic});
                l.text("name", TextDefinition{
                    .content = {.value = "ALEX MORGAN"},
                    .style = {
                        .font = {
                            .font_path = "assets/fonts/Poppins-Bold.ttf",
                            .font_family = "Poppins",
                            .font_weight = 700,
                            .font_size = 110.0f,
                        },
                        .color = Color{1.0f, 1.0f, 1.0f, 1.0f},
                    },
                    .frame = {.tracking = 14.0f},
                });
            });
            return s.build();
        });
}

} // anonymous namespace

// ── Regression lock ────────────────────────────────────────────────────────
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
    const auto bbox = chronon3d::test::completeness::alpha_bbox(*fb);

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

TEST_CASE("Full-canvas TextRun keeps parent center and produces visible ink") {
    struct CanvasCase {
        const char* label;
        int width;
        int height;
    };

    const CanvasCase cases[] = {
        {"16:9", 1920, 1080},
        {"9:16", 1080, 1920},
    };

    for (const auto& test_case : cases) {
        INFO("case=", test_case.label,
             " width=", test_case.width,
             " height=", test_case.height);

        auto renderer = test::make_renderer();
        auto comp = build_full_canvas_visible_comp(
            renderer,
            test_case.width,
            test_case.height);
        auto fb = renderer.render(comp, Frame{0});
        REQUIRE(static_cast<bool>(fb));

        const auto bbox = chronon3d::test::completeness::alpha_bbox(*fb);
        const int visible_pixels =
            chronon3d::test::completeness::count_visible_pixels(*fb);

        INFO("Full-canvas bbox: x0=", bbox.x0, " y0=", bbox.y0,
             " x1=", bbox.x1, " y1=", bbox.y1,
             " visible_pixels=", visible_pixels);

        CHECK_FALSE(bbox.empty());
        CHECK(visible_pixels > 100);
        CHECK(bbox.x0 >= 0);
        CHECK(bbox.y0 >= 0);
        CHECK(bbox.x1 < test_case.width);
        CHECK(bbox.y1 < test_case.height);

        const float actual_cx = (bbox.x0 + bbox.x1) * 0.5f;
        const float actual_cy = (bbox.y0 + bbox.y1) * 0.5f;
        const float expected_cx = static_cast<float>(test_case.width) * 0.5f;
        const float expected_cy = static_cast<float>(test_case.height) * 0.5f;
        const float tolerance_x = static_cast<float>(test_case.width) * 0.15f;
        const float tolerance_y = static_cast<float>(test_case.height) * 0.15f;

        CHECK(std::abs(actual_cx - expected_cx) < tolerance_x);
        CHECK(std::abs(actual_cy - expected_cy) < tolerance_y);
    }
}

TEST_CASE("Pinned absolute TextRun stays centered across animation boundary") {
    auto renderer = test::make_renderer();
    auto comp = build_pinned_absolute_animation_comp(renderer);

    for (const Frame frame : {Frame{21}, Frame{22}, Frame{23}, Frame{30}, Frame{59}}) {
        INFO("frame=", frame);
        auto fb = renderer.render(comp, frame);
        REQUIRE(static_cast<bool>(fb));

        const auto bbox = chronon3d::test::completeness::alpha_bbox(*fb);
        INFO("bbox: x0=", bbox.x0, " y0=", bbox.y0,
             " x1=", bbox.x1, " y1=", bbox.y1);
        CHECK_FALSE(bbox.empty());

        const float center_x = (bbox.x0 + bbox.x1) * 0.5f;
        CHECK(std::abs(center_x - 960.0f) <= 10.0f);

        if (frame >= Frame{22}) {
            const float center_y = (bbox.y0 + bbox.y1) * 0.5f;
            CHECK(std::abs(center_y - 540.0f) <= 30.0f);
        }
    }
}

// ── Anchor exactly-once probes (canvas 1920×1080) ──────────────────────────
// Canonical 5-pin × 3-anchor matrix.  Glyphs are laid out box-local;
// resolve_text_placement() consumes the box anchor ONCE when it derives
// layout_origin = pin − anchor_offset, and Transform::to_mat4() keeps
// world_transform.anchor zeroed — see the ANCHOR EXACTLY-ONCE CONTRACT
// block in src/scene/builders/layer_builder_text.cpp.
//
// Expected rendered ink centre is therefore a closed form:
//   ink_center = pin + (box_size/2) − anchor_offset(anchor)
// (alignment Center/Middle centres the ink inside the authored box, so
// this holds regardless of font metrics).

// [Removed] build_unpinned_absolute_center_comp + its implicit-center lock:
// the legacy "unpinned Absolute keeps ink off-center" contract was a symptom
// of the double anchor consumption. Superseded by the Anchor exactly-once
// matrix below, which pins the corrected closed-form pivot semantics.
namespace {

const Vec2 k_anchor_box{400.0f, 120.0f};

Composition build_anchor_probe_comp(
    SoftwareRenderer& renderer,
    const Vec2& pin,
    TextAnchor anchor,
    std::string_view uid)
{
    // uid disambiguates layer/node names per probe: renderer-session caches
    // (NodeCacheKey scope = "layer.textrun:<layer>:<node>") must never serve
    // one probe's raster for another. Without a suffix, equal content + equal
    // dimensions can collide across compositions despite differing matrices.
    const std::string layer_name = "probe_" + std::string(uid);
    return composition(
        {.name = "TextRun/anchor_probe_" + std::string(uid),
         .width = 1920,
         .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 1},
        [&renderer, pin, anchor, layer_name](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer(layer_name.c_str(), [&renderer, pin, anchor](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.text("run", TextDefinition{
                    .content = {.value = "AK"},
                    .style = {
                        .font = {
                            .font_path = "assets/fonts/Poppins-Bold.ttf",
                            .font_family = "Poppins",
                            .font_weight = 700,
                            .font_size = 48.0f,
                        },
                        .color = Color{1.0f, 1.0f, 1.0f, 1.0f},
                    },
                    .frame = {
                        .size = k_anchor_box,
                        .placement = TextPlacement{TextPlacementKind::Absolute, pin},
                        .anchor = anchor,
                    },
                });
            });
            return s.build();
        });
}

Vec2 expected_ink_center(const Vec2& pin, TextAnchor anchor)
{
    Vec2 anchor_offset{};
    switch (anchor) {
        case TextAnchor::TopLeft:     break;
        case TextAnchor::Center:      anchor_offset = Vec2{k_anchor_box * 0.5f}; break;
        default:                      anchor_offset = k_anchor_box; break;
    }
    return pin + Vec2{k_anchor_box * 0.5f} - anchor_offset;
}

const char* anchor_label(TextAnchor anchor)
{
    switch (anchor) {
        case TextAnchor::TopLeft: return "TopLeft";
        case TextAnchor::Center:  return "Center";
        default:                  return "BottomRight";
    }
}

} // anonymous namespace

TEST_CASE("Anchor exactly-once: 5 pins x 3 anchors land on closed-form pivot") {
    struct ProbeCase {
        const char* label;
        Vec2 pin;
    };
    // Inset >= 480 px so every (pin, anchor) box stays fully on-canvas:
    // clipping would corrupt the alpha-bbox pivot measurement.
    const ProbeCase pins[] = {
        {"TopLeft",     Vec2{480.0f, 330.0f}},
        {"TopRight",    Vec2{1440.0f, 330.0f}},
        {"Center",      Vec2{960.0f, 540.0f}},
        {"BottomLeft",  Vec2{480.0f, 750.0f}},
        {"BottomRight", Vec2{1440.0f, 750.0f}},
    };
    const TextAnchor anchors[] = {
        TextAnchor::TopLeft,      // (0, 0)
        TextAnchor::Center,       // (w/2, h/2)
        TextAnchor::BottomRight,  // (w, h)
    };

    for (const auto& pin_case : pins) {
        for (const TextAnchor anchor : anchors) {
            INFO("pin=", pin_case.label,
                 " anchor=", anchor_label(anchor));
            // Fresh session per probe: renderer-session caches (program
            // store / node cache) must not serve another probe's raster.
            auto renderer = test::make_renderer();
            auto comp = build_anchor_probe_comp(
                renderer, pin_case.pin, anchor, pin_case.label);
            auto fb = renderer.render(comp, Frame{0});
            REQUIRE(static_cast<bool>(fb));

            const auto bbox = chronon3d::test::completeness::alpha_bbox(*fb);
            CHECK_FALSE(bbox.empty());
            CHECK(chronon3d::test::completeness::count_visible_pixels(*fb) > 50);

            const Vec2 measured_center{
                (bbox.x0 + bbox.x1) * 0.5f,
                (bbox.y0 + bbox.y1) * 0.5f};
            const Vec2 expected = expected_ink_center(pin_case.pin, anchor);

            INFO("bbox: x0=", bbox.x0, " y0=", bbox.y0,
                 " x1=", bbox.x1, " y1=", bbox.y1);
            INFO("expected ink center: ", expected.x, ", ", expected.y);

            // Exactly-once: moving the anchor moves the pivot by exactly
            // one anchor_offset delta — never two.
            CHECK(std::abs(measured_center.x - expected.x) <= 10.0f);
            CHECK(std::abs(measured_center.y - expected.y) <= 12.0f);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════
// BBox parity certification (plan action 4)
// ═══════════════════════════════════════════════════════════════════════
// DONE criterion: actual_alpha_bbox ⊆ predicted placement box with
// text_bbox_contract_violations == 0 across a representative text corpus
// (headline / subtitle / lower-third / watermark intents through the
// canonical resolve_text_placement chain).  The conservative
// full-canvas fallbacks inside TextRunNode::predicted_bbox()
// (pre-clip degenerate + suspiciously-thin CONSERVATIVE_EXPAND + FU04
// audit expansion) stay in place as COUNTED safety nets; this corpus
// proves they are not exercised, i.e. fallback behavior has not become
// the normal path.
namespace {

raster::BBox bbox_from_frame(const Vec4& local_frame)
{
    return raster::BBox{
        static_cast<i32>(local_frame.x),
        static_cast<i32>(local_frame.y),
        static_cast<i32>(local_frame.x + local_frame.z),
        static_cast<i32>(local_frame.y + local_frame.w)};
}

template <typename BBoxLike>
bool expanded_contains(
    const raster::BBox& envelope, const BBoxLike& inner, int margin)
{
    return inner.x0 >= envelope.x0 - margin &&
           inner.y0 >= envelope.y0 - margin &&
           inner.x1 <= envelope.x1 + margin &&
           inner.y1 <= envelope.y1 + margin;
}

} // anonymous namespace

TEST_CASE("Canonical placement: headline subtitle and watermark resolve offsets once") {
    const CanvasInfo canvas = CanvasInfo::from_dimensions(1920.0f, 1080.0f);
    struct PlacementCase {
        const char* label;
        TextPlacement placement;
        TextAnchor anchor;
        Vec2 box;
        Vec2 expected_origin;
    };
    const PlacementCase cases[] = {
        {"headline", {TextPlacementKind::TopCenter, {0.0f, 50.0f}},
         TextAnchor::Center, {800.0f, 160.0f}, {560.0f, 24.0f}},
        {"subtitle", {TextPlacementKind::BottomCenter, {0.0f, -80.0f}},
         TextAnchor::Center, {700.0f, 100.0f}, {610.0f, 896.0f}},
        {"watermark", {TextPlacementKind::TopRight, {-30.0f, 30.0f}},
         TextAnchor::TopLeft, {240.0f, 50.0f}, {1794.0f, 84.0f}},
    };

    for (const auto& c : cases) {
        const auto resolved = resolve_text_placement(
            canvas, c.box, c.placement, c.anchor);
        INFO("case=", c.label);
        CHECK(resolved.layout_origin.x == doctest::Approx(c.expected_origin.x));
        CHECK(resolved.layout_origin.y == doctest::Approx(c.expected_origin.y));
        CHECK(resolved.local_frame.x == doctest::Approx(c.expected_origin.x));
        CHECK(resolved.local_frame.y == doctest::Approx(c.expected_origin.y));
    }
}

TEST_CASE("Canonical placement: absolute offset is the pin, not an extra delta") {
    const auto resolved = resolve_text_placement(
        CanvasInfo::from_dimensions(1920.0f, 1080.0f),
        Vec2{400.0f, 120.0f},
        TextPlacement{TextPlacementKind::Absolute, {960.0f, 540.0f}},
        TextAnchor::Center);
    CHECK(resolved.layout_origin.x == doctest::Approx(760.0f));
    CHECK(resolved.layout_origin.y == doctest::Approx(480.0f));
}

TEST_CASE("BBoxParity: text corpus ink stays inside predicted placement box") {
    // ── Corpus definitions (canonical base presets + watermark surrogate). ──
    const CanvasInfo canvas = CanvasInfo::from_dimensions(1920.0f, 1080.0f);

    TextDefinition headline = presets::text::title_centered("CORPUS HEADLINE", canvas);
    headline.frame.overflow = TextOverflow::Clip;

    TextDefinition subtitle = presets::text::subtitle_bottom("corpus subtitle line", canvas);

    TextDefinition lower = presets::text::lower_third("CORPUS LOWER THIRD", canvas);

    // Watermark surrogate: TopRight safe-area pin, box anchored to grow
    // down-left INTO the canvas (BottomRight box anchor).
    TextDefinition watermark{};
    watermark.content.value = "WM";
    watermark.style.font.font_path = "assets/fonts/Poppins-Bold.ttf";
    watermark.style.font.font_size = 28.0f;
    watermark.style.color = Color{1.0f, 1.0f, 1.0f, 1.0f};
    watermark.frame.size = Vec2{240.0f, 50.0f};
    watermark.frame.placement = TextPlacement{TextPlacementKind::TopRight};
    watermark.frame.anchor = TextAnchor::BottomRight;
    watermark.frame.align = TextAlign::Right;
    watermark.frame.vertical_align = VerticalAlign::Top;

    struct CorpusCase {
        const char* label;
        const TextDefinition* def;
    };
    const CorpusCase cases[] = {
        {"headline",  &headline},
        {"subtitle",  &subtitle},
        {"lower3rd",  &lower},
        {"watermark", &watermark},
    };

    for (const auto& c : cases) {
        // Predicted envelope straight from the canonical resolver — the
        // SAME authority the graph builder consumes.
        const auto resolved = resolve_text_placement(
            canvas, c.def->frame.size, c.def->frame.placement,
            c.def->frame.anchor);
        const raster::BBox envelope = bbox_from_frame(resolved.local_frame);

        INFO("case=", c.label,
             " envelope: x0=", envelope.x0, " y0=", envelope.y0,
             " x1=", envelope.x1, " y1=", envelope.y1);

        // A predicted box that leaks off-canvas means the intent itself
        // is wrong — fail here, not as silent clipping.
        CHECK(envelope.x0 >= 0);
        CHECK(envelope.y0 >= 0);
        CHECK(envelope.x1 <= 1920);
        CHECK(envelope.y1 <= 1080);

        // Fresh renderer per case: session caches and violation counters
        // stay isolated so the zero-violation evidence is unambiguous.
        auto renderer = test::make_renderer();

        auto comp = composition(
            {.name = std::string("TextRun/bbox_parity_") + c.label,
             .width = 1920,
             .height = 1080,
             .frame_rate = FrameRate{30, 1},
             .duration = 1},
            [&renderer, def = *c.def, name = c.label](const FrameContext& ctx) -> Scene {
                SceneBuilder s(ctx);
                s.font_engine(&renderer.font_engine());
                s.layer(name, [&renderer, &def](LayerBuilder& l) {
                    l.font_engine(&renderer.font_engine());
                    l.text("run", def);
                });
                return s.build();
            });

        auto fb = renderer.render(comp, Frame{0});
        REQUIRE(static_cast<bool>(fb));

        const auto bbox = chronon3d::test::completeness::alpha_bbox(*fb);
        INFO("bbox: x0=", bbox.x0, " y0=", bbox.y0,
             " x1=", bbox.x1, " y1=", bbox.y1,
             " visible=",
             chronon3d::test::completeness::count_visible_pixels(*fb));

        // Real ink exists…
        CHECK_FALSE(bbox.empty());
        CHECK(chronon3d::test::completeness::count_visible_pixels(*fb) > 60);

        // …and stays INSIDE the predicted placement box (±AA margin).
        constexpr int kPad = 28;
        CHECK(expanded_contains(envelope, bbox, kPad));

        // Zero contract-violation counter bumps for this frame.
        REQUIRE(renderer.counters() != nullptr);
        CHECK(renderer.counters()->text_bbox_contract_violations.load() == 0);
    }
}
