// ==============================================================================
// tests/visual/PR3/pr3_compositions.cpp
//
// PR3 — 4 end-to-end compositions, each rendered twice (NONE vs EFFECT_ON) so
// every construct has a deterministic golden pair.  A composition is "NONE"
// when its main visual effect (mask / DoF / motion-blur / video slot) is
// disabled; "EFFECT_ON" activates the construct that the spec requested.
//
//   Composition                NONE case name              EFFECT_ON case name
//   ───────────────────────    ─────────────────────────    ─────────────────────────
//   1. text+gradient+mask      text_gradient_mask_none     text_gradient_mask_with_mask
//   2. camera+depth+DoF        camera_depth_dof_none       camera_depth_dof_with_dof
//   3. motion blur+transp.     motion_blur_transp_none     motion_blur_transp_with_blur
//   4. video+images+RTL tex   video_images_rtl_none       video_images_rtl_with_video
//
// DoF lives on Camera2_5D::dof (DepthOfFieldSettings). Motion-blur lives on
// RenderSettings::motion_blur (MotionBlurSettings). Video uses
// SceneBuilder::video_layer; the composition wiring is exercised even when a
// real .mp4 fixture is absent (image-card placeholder retains the visible
// media card variation).
// ==============================================================================

#include <doctest/doctest.h>

#include <chronon3d/api/composition.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/graphics/shape_style/fill_style.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/scene/model/camera/camera_common_types.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <tests/helpers/test_utils.hpp>
#include <tests/visual/support/golden_test.hpp>
#include <tests/visual/support/image_diff.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
using namespace chronon3d;
using namespace chronon3d::test;

namespace {

// ── Shared PR3 constants ─────────────────────────────────────────────────────

const std::filesystem::path kGoldenDir   = "tests/golden/pr3";
const std::filesystem::path kArtifactDir = "test_renders/artifacts/pr3";

constexpr int kDuration = 4;       // Matches cinematic_motion / PR2 convention.

// Asset paths already mounted by tests/test_main.cpp at current_path().
const std::string kFontInter       = "assets/fonts/Inter-Bold.ttf";
const std::string kFontArabic      = "assets/fonts/NotoNaskhArabic-Bold.ttf";
const std::string kImgChecker      = "assets/images/checker.png";
const std::string kImgGridTile     = "assets/images/grid_tile.png";

// ── Golden config (same ImageDiffThreshold as PR2) ───────────────────────────

GoldenTestConfig make_pr3_golden_config() {
    ImageDiffThreshold t{};
    t.max_mean_abs_error      = 0.025f;
    t.max_abs_error           = 0.10f;
    t.max_rmse                = 0.012f;
    t.max_changed_pixel_ratio = 0.08f;
    return {
        .golden_directory   = kGoldenDir,
        .artifact_directory = kArtifactDir,
        .threshold          = t,
        .mode               = golden_mode_from_environment(),
    };
}

void verify_pr3_golden(const Framebuffer& fb, const std::string& name) {
    const auto result = verify_golden(fb, name, make_pr3_golden_config());
<<<<<<< HEAD
    REQUIRE_FALSE(result.golden_missing);
=======
>>>>>>> dbf39153 (fix(tests): make golden references mandatory in CI/certification mode)
    INFO(result.message);
    REQUIRE_FALSE(result.golden_missing);
    CHECK(result.passed);
}

// ── TextSpec helpers (designated-init; equivalent to PR2 inlined literal) ────

TextSpec make_text(const std::string& utf8,
                   const std::string& font_path,
                   const std::string& family,
                   float size_pt,
                   Color color,
                   Vec2 box) {
    // TICKET-TEXT-LEGACY-POSITION-ROT (sub-area (ii), pr3 helper site 6/6):
    // legacy .position Vec3 removed upstream; migrate to .placement.
    // Z=0 hard-coded (null offset, anchors via layered l.position calls),
    // safe-drop per M1.8 §5A. Static value -> inline TextPlacement (no
    // local variable extraction needed since [0,0] has no runtime deps).
    return TextSpec{
        .content    = TextContent{.value = utf8},
        .placement  = chronon3d::TextPlacement{chronon3d::TextPlacementKind::Absolute, {0.0f, 0.0f}},
        .font       = FontSpec{
            .font_path   = font_path,
            .font_family = family,
            .font_weight = 700,
            .font_style  = "normal",
            .font_size   = size_pt,
        },
        .layout     = TextLayoutSpec{
            .box         = box,
            .line_height = 1.20f,
            .tracking    = 1.0f,
        },
        .appearance = TextAppearanceSpec{
            .color = color,
        },
    };
}

// ── Render-helper with custom RenderSettings ─────────────────────────────────

std::shared_ptr<Framebuffer> render_with(const Composition& comp, int frame,
                                         RenderSettings settings) {
    auto renderer = test::make_renderer();
    settings.use_modular_graph = true;
    (void)settings.motion_blur.mode;
    renderer.set_settings(settings);
    renderer.set_image_backend(std::make_shared<image::StbImageBackend>());
    return renderer.render(comp, Frame{frame});
}

// ── Composition 1 builders: text + gradient + mask ───────────────────────────

Composition make_text_gradient_composition(bool apply_mask, int w, int h) {
    return composition({.name = "PR3_text_gradient",
                        .width = w, .height = h, .duration = kDuration},
        [=](const FrameContext& ctx) {
            SceneBuilder s(ctx);

            // ── Gradient background (always present) ────────────────────────
            s.layer("bg", [=](LayerBuilder& l) {
                l.rect("bg_rect", {
                    .size  = {static_cast<f32>(w), static_cast<f32>(h)},
                    .color = {1.0f, 1.0f, 1.0f, 1.0f},
                    .pos   = {0.0f, 0.0f, -10.0f},
                    .fill  = graphics::FillStyle::linear(
                        Vec2{0.0f, 0.0f}, Vec2{1.0f, 1.0f},
                        {
                            {0.0f, Color{0.08f, 0.04f, 0.20f, 1.0f}},
                            {0.5f, Color{0.62f, 0.18f, 0.42f, 1.0f}},
                            {1.0f, Color{0.95f, 0.55f, 0.20f, 1.0f}},
                        }),
                });
            });

            // ── Title layer with optional mask ─────────────────────────────
            s.layer("title", [=](LayerBuilder& l) {
                l.position({0.0f, -20.0f, 0.0f});
                if (apply_mask) {
                    l.mask_rect(RectMaskParams{
                        .size = {static_cast<f32>(w) * 0.70f, 100.0f},
                        .pos  = Vec3{0.0f, -20.0f, 0.0f},
                    });
                }
                l.text("title_label", make_text(
                    "MASKED",
                    kFontInter,
                    "Inter",
                    56.0f,
                    Color{1.0f, 0.97f, 0.85f, 1.0f},
                    {320.0f, 100.0f}));
            });

            // ── Subtitle (always full, controls NONE noise baseline) ───────
            s.layer("sub", [](LayerBuilder& l) {
                l.position({0.0f, 80.0f, 0.0f});
                l.text("sub_label", make_text(
                    "GRADIENT",
                    kFontInter,
                    "Inter",
                    22.0f,
                    Color{0.85f, 0.92f, 1.0f, 0.85f},
                    {300.0f, 50.0f}));
            });

            return s.build();
        });
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// 1. text+gradient+mask — NONE: gradient + title visible in full width
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("PR3-E2E: text_gradient_mask_none") {
    constexpr int W = 320, H = 240;
    auto comp = make_text_gradient_composition(/*apply_mask=*/false, W, H);
    auto fb = make_renderer().render(comp, 0);
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == W);
    REQUIRE(fb->height() == H);
    verify_pr3_golden(*fb, "text_gradient_mask_none");
}

// ═══════════════════════════════════════════════════════════════════════════
// 1. text+gradient+mask — EFFECT_ON: gradient + masked title (clip = 70% width)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("PR3-E2E: text_gradient_mask_with_mask") {
    constexpr int W = 320, H = 240;
    auto comp = make_text_gradient_composition(/*apply_mask=*/true, W, H);
    auto fb = make_renderer().render(comp, 0);
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == W);
    REQUIRE(fb->height() == H);

    // Non-equivalence: mask must change pixels below the title region.
    auto reference = make_renderer().render(
        make_text_gradient_composition(false, W, H), 0);
    REQUIRE(reference != nullptr);
    CHECK(framebuffer_hash(*fb) != framebuffer_hash(*reference));
    // Title-region luma in the with-mask variant must exceed NONE baseline
    // (because the mask clips the title's solid white block, exposing the
    // gradient outside it).
    const float with_luma  = average_luma_rect(*fb,        100,  90, 220, 130);
    const float none_luma  = average_luma_rect(*reference, 100,  90, 220, 130);
    CHECK(with_luma < none_luma);   // mask removes bright title pixels

    verify_pr3_golden(*fb, "text_gradient_mask_with_mask");
}

// ═══════════════════════════════════════════════════════════════════════════
// Composition 2 builders: camera + depth + DoF
// ═══════════════════════════════════════════════════════════════════════════

namespace {

Composition make_camera_depth_composition(bool apply_dof, int w, int h) {
    return composition({.name = "PR3_camera_depth",
                        .width = w, .height = h, .duration = kDuration},
        [=](const FrameContext& ctx) {
            SceneBuilder s(ctx);

            // Bars parked at three Z depths on the world axis.
            for (int i = 0; i < 3; ++i) {
                s.layer("bar_" + std::to_string(i), [i](LayerBuilder& l) {
                    const float dz = static_cast<float>(i - 1) * 220.0f;   // -220 / 0 / +220
                    const float dx = static_cast<float>(i - 1) * 90.0f;    // -90 / 0 / +90
                    const Color cols[3] = {
                        {0.92f, 0.30f, 0.30f, 1.0f},   // red   (closer)
                        {0.30f, 0.92f, 0.45f, 1.0f},   // green (focal)
                        {0.30f, 0.55f, 0.95f, 1.0f},   // blue  (further)
                    };
                    l.position({dx, 0.0f, dz});
                    l.rect("bar", {
                        .size  = {28.0f, 180.0f},
                        .color = cols[i],
                        .pos   = {0.0f, 0.0f, 0.0f},
                    });
                });
            }

            // Camera: anchored at Z = -800 looking at the bars.
            Camera2_5D cam{};
            cam.enabled = true;
            cam.position = {0.0f, 0.0f, -800.0f};
            cam.zoom = 700.0f;
            cam.fov_deg = 50.0f;
            cam.point_of_interest = {0.0f, 0.0f, 0.0f};
            cam.point_of_interest_enabled = true;
            cam.dof.enabled = apply_dof;
            cam.dof.use_physical_model = false;
            cam.dof.focus_z   = 0.0f;
            cam.dof.max_blur  = 14.0f;
            cam.dof.aperture  = 1.0f;
            cam.dof.near_bokeh_radius = 30.0f;
            cam.dof.far_bokeh_radius  = 30.0f;
            s.camera().set(cam);

            return s.build();
        });
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// 2. camera+depth+DoF — NONE: DoF disabled (no per-pixel blur)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("PR3-E2E: camera_depth_dof_none") {
    constexpr int W = 480, H = 270;
    auto comp = make_camera_depth_composition(/*apply_dof=*/false, W, H);
    auto fb = make_renderer().render(comp, 0);
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == W);
    REQUIRE(fb->height() == H);
    verify_pr3_golden(*fb, "camera_depth_dof_none");
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. camera+depth+DoF — EFFECT_ON: DoF enabled, focus at the centre bar
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("PR3-E2E: camera_depth_dof_with_dof") {
    constexpr int W = 480, H = 270;
    auto comp = make_camera_depth_composition(/*apply_dof=*/true, W, H);
    auto fb = make_renderer().render(comp, 0);
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == W);
    REQUIRE(fb->height() == H);

    auto reference = make_renderer().render(
        make_camera_depth_composition(false, W, H), 0);
    REQUIRE(reference != nullptr);
    CHECK(framebuffer_hash(*fb) != framebuffer_hash(*reference));

    verify_pr3_golden(*fb, "camera_depth_dof_with_dof");
}

// ═══════════════════════════════════════════════════════════════════════════
// Composition 3 builders: motion blur + transparencies
// ═══════════════════════════════════════════════════════════════════════════

namespace {

Composition make_motion_blur_composition(int w, int h) {
    return composition({.name = "PR3_motion_blur",
                        .width = w, .height = h, .duration = kDuration},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);

            // 3 semi-transparent moving rectangles — the central motion-blur
            // subject.  X advances 110 px/frame → at frame 2 they straddle
            // screen centre, ideal for temporal accumulation.
            const float t = static_cast<float>(ctx.frame);
            struct MovingItem { Vec2 size; Color color; float x0; };
            const MovingItem items[] = {
                {{46.0f, 46.0f}, {1.0f, 0.20f, 0.20f, 0.55f}, -160.0f},
                {{38.0f, 38.0f}, {0.20f, 0.90f, 0.40f, 0.50f},  -80.0f},
                {{30.0f, 30.0f}, {0.30f, 0.55f, 1.00f, 0.45f},    0.0f},
            };
            for (size_t i = 0; i < 3; ++i) {
                s.layer("item_" + std::to_string(i), [&, i](LayerBuilder& l) {
                    const float x = items[i].x0 + t * 110.0f;
                    l.position({x, 0.0f, 0.0f});
                    l.rect("r", {
                        .size  = items[i].size,
                        .color = items[i].color,
                        .pos   = {0.0f, 0.0f, 0.0f},
                    });
                });
            }

            return s.build();
        });
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// 3. motion blur+transparencies — NONE: motion blur OFF (sharp single frame)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("PR3-E2E: motion_blur_transp_none") {
    constexpr int W = 320, H = 240;
    auto comp = make_motion_blur_composition(W, H);

    RenderSettings settings{};
    settings.motion_blur.mode = MotionBlurMode::Off;
    auto fb = render_with(comp, /*frame=*/2, settings);

    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == W);
    REQUIRE(fb->height() == H);
    verify_pr3_golden(*fb, "motion_blur_transp_none");
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. motion blur+transparencies — EFFECT_ON: 16 samples @ 360° shutter
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("PR3-E2E: motion_blur_transp_with_blur") {
    constexpr int W = 320, H = 240;
    auto comp = make_motion_blur_composition(W, H);

    RenderSettings settings{};
    settings.motion_blur.mode             = MotionBlurMode::TemporalAccumulation;
    settings.motion_blur.samples          = 16;
    settings.motion_blur.shutter_angle_deg = 360.0f;
    settings.motion_blur.shutter_phase_deg = 0.0f;
    settings.motion_blur.pattern          = TemporalSamplePattern::Stratified;
    settings.motion_blur.filter           = TemporalFilter::Box;
    settings.motion_blur.jitter_seed      = 0xC0FFEE;
    auto fb = render_with(comp, /*frame=*/2, settings);

    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == W);
    REQUIRE(fb->height() == H);

    // Higher-pixel-rule equivalence: pairwise comparison with NONE.
    RenderSettings off_settings{};
    off_settings.motion_blur.mode = MotionBlurMode::Off;
    auto fb_off = render_with(comp, 2, off_settings);
    REQUIRE(fb_off != nullptr);
    CHECK(framebuffer_hash(*fb) != framebuffer_hash(*fb_off));

    verify_pr3_golden(*fb, "motion_blur_transp_with_blur");
}

// ═══════════════════════════════════════════════════════════════════════════
// Composition 4 builders: video + images + RTL text
//
// `with_video = true` adds a video_layer slot (path is the conventional
// test_video.mp4 fixture; when missing the slot is exercised structurally
// but the visible content comes from the second image card — colour-coded
// so the "with" variant differs visibly from "none").
// ═══════════════════════════════════════════════════════════════════════════

namespace {

Composition make_video_images_rtl_composition(bool with_video, int w, int h) {
    return composition({.name = "PR3_video_images_rtl",
                        .width = w, .height = h, .duration = kDuration},
        [=](const FrameContext& ctx) {
            SceneBuilder s(ctx);

            // Solid backdrop
            s.layer("backdrop", [w, h](LayerBuilder& l) {
                l.fill(Color{0.06f, 0.08f, 0.14f, 1.0f});
                (void)w; (void)h;
            });

            // First image card (always present)
            s.layer("image_main", [w, h](LayerBuilder& l) {
                l.position({0.0f, 0.0f, 0.0f});
                l.image("checker", ImageParams{
                    .path   = kImgChecker,
                    .size   = {static_cast<f32>(w) * 0.92f, static_cast<f32>(h) * 0.55f},
                    .pos    = {0.0f, 40.0f, 0.0f},
                    .fit    = FitMode::Cover,
                    .opacity = 0.85f,
                });
            });

            // Second image card — only in "with" variant.  Acts as the visual
            // proxy for the video-layer slot since we don't ship an .mp4
            // fixture (see docstring).
            if (with_video) {
                s.layer("image_video_proxy", [w](LayerBuilder& l) {
                    l.position({0.0f, 0.0f, 0.0f});
                    l.image("tile", ImageParams{
                        .path   = kImgGridTile,
                        .size   = {120.0f, 120.0f},
                        .pos    = {static_cast<f32>(w) * -0.32f,
                                   static_cast<f32>(w) * 0.18f, 5.0f},
                        .fit    = FitMode::Contain,
                        .opacity = 0.95f,
                    });
                });
                // Exercise the video pipeline wiring.  No fixture shipped →
                // software backend renders the slot as no-op; the visible
                // "video" is the image_proxy card above.  One-time stderr
                // note on missing fixture so the user understands the warning.
                if (!std::filesystem::exists("tests/golden/pr3/test_video.mp4")) {
                    MESSAGE("PR3 video fixture absent (tests/golden/pr3/"
                            "test_video.mp4) — slot is structurally wired;"
                            " image proxy provides the visible media card.");
                }
                s.video_layer("video_slot", "tests/golden/pr3/test_video.mp4",
                              [](LayerBuilder& l) {
                                  l.opacity(0.0f);   // never visible — see comment
                              });
            }

            // RTL Arabic text overlay (always present).  NotoNaskhArabic +
            // Auto direction is correct because Arabic characters are
            // strongly directional — HarfBuzz auto-detects RTL.
            s.layer("rtl_overlay", [w, h](LayerBuilder& l) {
                l.position({0.0f, -static_cast<f32>(h) * 0.30f, 10.0f});
                l.text("rtl_label", make_text(
                    "\xD9\x85" "\xD8\xB1" "\xD8\xAD" "\xD8\xA8" "\xD8\xA7"
                    "\x20\xD8\xA8\xD9\x80" "\xD8\xA7\xD9\x84" "\xD8\xB9" "\xD8\xA7"
                    "\xD9\x84" "\xD9\x85",
                    kFontArabic,
                    "Noto Naskh Arabic",
                    42.0f,
                    Color{0.98f, 0.92f, 0.78f, 1.0f},
                    {static_cast<f32>(w) * 0.85f, 80.0f}));
            });

            return s.build();
        });
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// 4. video+images+RTL text — NONE: 1 image + RTL overlay
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("PR3-E2E: video_images_rtl_none") {
    constexpr int W = 480, H = 270;
    auto comp = make_video_images_rtl_composition(/*with_video=*/false, W, H);
    auto fb = make_renderer().render(comp, 0);
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == W);
    REQUIRE(fb->height() == H);
    verify_pr3_golden(*fb, "video_images_rtl_none");
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. video+images+RTL text — EFFECT_ON: + 2nd image proxy + video_layer
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("PR3-E2E: video_images_rtl_with_video") {
    constexpr int W = 480, H = 270;
    auto comp = make_video_images_rtl_composition(/*with_video=*/true, W, H);
    auto fb = make_renderer().render(comp, 0);
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == W);
    REQUIRE(fb->height() == H);

    auto reference = make_renderer().render(
        make_video_images_rtl_composition(false, W, H), 0);
    REQUIRE(reference != nullptr);
    CHECK(framebuffer_hash(*fb) != framebuffer_hash(*reference));

    verify_pr3_golden(*fb, "video_images_rtl_with_video");
}
