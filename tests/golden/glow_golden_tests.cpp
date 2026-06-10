#include <doctest/doctest.h>

#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/media/media_placement.hpp>
#include <chronon3d/presets/motion_object.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/model/core/effect_stack.hpp>
#include <tests/helpers/test_utils.hpp>

// Typewriter pipeline header — exercises the motion_object → make_typewriter
// → GlowBloom preset → text_glow.cpp path.  This is the production path
// used by content::text::text_glow_reveal(), so regressions on the
// typewriter pipeline (per-layer glow strengths, double-glow routing,
// blur of the sharp text, etc.) are caught here rather than going
// through LayerBuilder::glow() directly.
#include <content/text/typewriter/typewriter_common.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <string>

using namespace chronon3d;
using namespace chronon3d::test;

namespace {

struct GlowComparisonResult {
    bool success{true};
    float max_channel_diff{0.0f};
    float mean_error{0.0f};
    float mismatch_percentage{0.0f};
    std::string error_message;
};

GlowComparisonResult compare_glow_images(const Framebuffer& rendered, const Framebuffer& golden) {
    GlowComparisonResult res;
    if (rendered.width() != golden.width() || rendered.height() != golden.height()) {
        res.success = false;
        res.error_message = "Dimension mismatch: rendered=" + std::to_string(rendered.width()) + "x" + std::to_string(rendered.height()) +
                            ", golden=" + std::to_string(golden.width()) + "x" + std::to_string(golden.height());
        return res;
    }

    double total_channel_diff = 0.0;
    int mismatched_pixels = 0;
    const int total_pixels = rendered.width() * rendered.height();

    for (int y = 0; y < rendered.height(); ++y) {
        for (int x = 0; x < rendered.width(); ++x) {
            // Both rendered and golden are compared in [0,1] sRGB because PNG
        // roundtrip clamps to 8-bit. HDR glow accumulation (>1.0) must be
        // clamped before comparison (the visual difference is in the glow
        // intensity, not in the clipped highlights).
        const Color c1 = rendered.get_pixel(x, y).to_srgb().clamped();
        const Color c2 = golden.get_pixel(x, y).clamped();

            const float dr = std::abs(c1.r - c2.r);
            const float dg = std::abs(c1.g - c2.g);
            const float db = std::abs(c1.b - c2.b);
            const float da = std::abs(c1.a - c2.a);

            const float max_diff = std::max({dr, dg, db, da});
            res.max_channel_diff = std::max(res.max_channel_diff, max_diff);
            total_channel_diff += dr + dg + db + da;

            if (max_diff > 3.0f / 255.0f) {
                ++mismatched_pixels;
            }
        }
    }

    res.mean_error = static_cast<float>(total_channel_diff / (total_pixels * 4));
    res.mismatch_percentage = static_cast<float>(mismatched_pixels) / static_cast<float>(total_pixels);

    if (res.mismatch_percentage > 0.02f || res.mean_error >= 0.01f || res.max_channel_diff >= 7.0f / 255.0f) {
        res.success = false;
        res.error_message = "Threshold exceeded: maxPixelDiff=" + std::to_string(res.mismatch_percentage * 100.0f) + "%" +
                            ", meanError=" + std::to_string(res.mean_error) +
                            ", maxChannelError=" + std::to_string(res.max_channel_diff * 255.0f) + "/255";
    }

    return res;
}

void verify_glow_golden_or_create(const Framebuffer& rendered, const std::string& filename) {
    const std::filesystem::path golden_dir = "test_renders/golden/glow";
    std::filesystem::create_directories(golden_dir);
    const std::filesystem::path golden_path = golden_dir / filename;

    if (!std::filesystem::exists(golden_path)) {
        REQUIRE(save_png(rendered, golden_path.string()));
    }

    auto golden = load_png_as_framebuffer(golden_path.string());
    REQUIRE(golden != nullptr);

    const auto comp = compare_glow_images(rendered, *golden);
    INFO(comp.error_message);
    CHECK(comp.success);
}

#define verify_golden_or_create verify_glow_golden_or_create

Composition make_neon_card() {
    return Composition({.width = 960, .height = 540, .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("bg", [](LayerBuilder& l) {
            l.fill({0.02f, 0.03f, 0.07f, 1.0f});
        });

        const std::array<float, 4> radii{{18.0f, 32.0f, 52.0f, 84.0f}};
        const std::array<float, 4> xs{{-300.0f, -100.0f, 100.0f, 300.0f}};
        const std::array<Color, 4> colors{{
            {0.15f, 0.65f, 1.0f, 1.0f},
            {0.30f, 0.95f, 1.0f, 1.0f},
            {1.00f, 0.74f, 0.22f, 1.0f},
            {1.00f, 0.22f, 0.72f, 1.0f},
        }};

        for (int i = 0; i < 4; ++i) {
            s.layer("orb_" + std::to_string(i), [=](LayerBuilder& l) {
                l.position({xs[i], -30.0f, 0.0f})
                 .glow(radii[i] * 1.6f, 1.0f + 0.15f * i, colors[i]);
                l.circle("c", {
                    .radius = radii[i],
                    .color = Color{1.0f, 1.0f, 1.0f, 0.95f},
                    .pos = {0.0f, 0.0f, 0.0f}
                });
            });
        }

        s.layer("title", [](LayerBuilder& l) {
            l.position({0.0f, 170.0f, 0.0f})
             .glow(GlowPresets::neon_blue(44.0f));
            l.text("title", {
                .text = "NEON CARD",
                .size = {700.0f, 120.0f},
                .pos = {0.0f, 0.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .font_family = "Inter",
                .font_size = 72.0f,
                .color = Color{0.98f, 0.99f, 1.0f, 1.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle
            });
        });

        s.layer("caption", [](LayerBuilder& l) {
            l.position({0.0f, 245.0f, 0.0f})
             .glow(GlowPresets::cinematic_gold(24.0f));
            l.text("caption", {
                .text = "radius ladder + halo balance",
                .size = {680.0f, 48.0f},
                .pos = {0.0f, 0.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Regular.ttf",
                .font_family = "Inter",
                .font_size = 22.0f,
                .color = Color{0.82f, 0.86f, 0.94f, 1.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle
            });
        });

        return s.build();
    });
}

Composition make_text_glow_scene() {
    return Composition({.width = 960, .height = 540, .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("bg", [](LayerBuilder& l) {
            l.fill({0.01f, 0.015f, 0.04f, 1.0f});
        });

        s.layer("hero", [](LayerBuilder& l) {
            l.position({0.0f, -10.0f, 0.0f})
             .glow(GlowPresets::cinematic_gold(52.0f));
            l.text("hero", {
                .text = "GLOW ENGINE",
                .size = {820.0f, 160.0f},
                .pos = {0.0f, 0.0f, 0.0f},
                .font_path = "assets/fonts/Georgia_Bold.ttf",
                .font_family = "Georgia",
                .font_size = 78.0f,
                .color = Color{1.0f, 0.97f, 0.88f, 1.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle
            });
        });

        s.layer("sub", [](LayerBuilder& l) {
            l.position({0.0f, 128.0f, 0.0f})
             .glow(GlowPresets::soft_cyan(28.0f));
            l.text("sub", {
                .text = "text, image and shape coverage",
                .size = {760.0f, 52.0f},
                .pos = {0.0f, 0.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Regular.ttf",
                .font_family = "Inter",
                .font_size = 24.0f,
                .color = Color{0.76f, 0.92f, 1.0f, 1.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle
            });
        });

        return s.build();
    });
}

Composition make_image_glow_scene() {
    return Composition({.width = 960, .height = 540, .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("bg", [](LayerBuilder& l) {
            l.fill({0.015f, 0.015f, 0.025f, 1.0f});
        });

        s.layer("image_plate", [](LayerBuilder& l) {
            l.position({0.0f, 0.0f, 0.0f})
             .glow(GlowPresets::soft_cyan(36.0f));
            l.image("checker", {
                .path = "assets/images/checker.png",
                .size = {280.0f, 280.0f},
                .pos = {-140.0f, -140.0f, 0.0f},
                .fit = FitMode::Contain,
                .opacity = 0.98f,
                .radius = 16.0f
            });
        });

        s.layer("label", [](LayerBuilder& l) {
            l.position({0.0f, 190.0f, 0.0f})
             .glow(GlowPresets::neon_blue(20.0f));
            l.text("label", {
                .text = "image glow alpha coverage",
                .size = {760.0f, 48.0f},
                .pos = {0.0f, 0.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Regular.ttf",
                .font_family = "Inter",
                .font_size = 22.0f,
                .color = Color{0.86f, 0.90f, 0.98f, 1.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle
            });
        });

        return s.build();
    });
}

Composition make_edge_glow_scene() {
    return Composition({.width = 960, .height = 540, .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("bg", [](LayerBuilder& l) {
            l.fill({0.01f, 0.012f, 0.03f, 1.0f});
        });

        s.layer("edge_orb", [](LayerBuilder& l) {
            l.position({-400.0f, -220.0f, 0.0f})
             .glow(GlowPresets::neon_blue(96.0f));
            l.circle("orb", {
                .radius = 84.0f,
                .color = Color{0.98f, 0.99f, 1.0f, 0.95f},
                .pos = {0.0f, 0.0f, 0.0f}
            });
        });

        s.layer("counter_orb", [](LayerBuilder& l) {
            l.position({290.0f, 150.0f, 0.0f})
             .glow(GlowPresets::cinematic_gold(72.0f));
            l.circle("orb", {
                .radius = 52.0f,
                .color = Color{1.0f, 0.92f, 0.72f, 0.96f},
                .pos = {0.0f, 0.0f, 0.0f}
            });
        });

        return s.build();
    });
}

Composition make_pulse_scene() {
    return Composition({.width = 960, .height = 540, .duration = 30}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("bg", [](LayerBuilder& l) {
            l.fill({0.01f, 0.02f, 0.04f, 1.0f});
        });

        const float t = static_cast<float>(ctx.frame.frame) / 30.0f;
        const float pulse = 0.5f + 0.5f * std::sin(t * 6.2831853f * 2.0f);
        const float aura = 0.5f + 0.5f * std::sin(t * 6.2831853f * 2.0f + 1.2f);

        s.layer("core", [pulse](LayerBuilder& l) {
            l.position({0.0f, -20.0f, 0.0f})
             .glow(30.0f + pulse * 12.0f, 1.5f + pulse * 0.5f, Color{0.95f, 0.98f, 1.0f, 1.0f});
            l.circle("core", {
                .radius = 34.0f + pulse * 7.0f,
                .color = Color{1.0f, 1.0f, 1.0f, 0.96f},
                .pos = {0.0f, 0.0f, 0.0f}
            });
        });

        s.layer("aura", [aura](LayerBuilder& l) {
            l.position({0.0f, -20.0f, 0.0f})
             .glow(72.0f + aura * 18.0f, 0.95f + aura * 0.35f, Color{0.20f, 0.78f, 1.0f, 1.0f});
            l.circle("aura", {
                .radius = 108.0f,
                .color = Color{0.20f, 0.78f, 1.0f, 0.16f},
                .pos = {0.0f, 0.0f, 0.0f}
            });
        });

        s.layer("title", [pulse](LayerBuilder& l) {
            l.position({0.0f, 180.0f, 0.0f})
             .glow(GlowPresets::soft_cyan(22.0f + pulse * 4.0f));
            l.text("title", {
                .text = "PULSE",
                .size = {520.0f, 84.0f},
                .pos = {0.0f, 0.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .font_family = "Inter",
                .font_size = 56.0f,
                .color = Color{0.78f, 0.94f, 1.0f, 1.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle
            });
        });

        return s.build();
    });
}

// ── Motion-object / typewriter golden scenes ────────────────────────────────────
//
// These exercise the PRODUCTION path used by content::text::text_glow_reveal():
//   make_typewriter() → MotionObject::text() → motion_renderer →
//   GlowBloom preset → text_glow.cpp → draw_text_glow()
//
// They catch regressions that the LayerBuilder-direct tests above cannot:
//   - hardcoded per-layer glow strengths (text_glow.cpp's kGlowLayers)
//   - double-glow routing when both .glow(true) and GlowBloom are set
//   - sharp-text blur from st.blur in the GlowBloom preset
//   - cursor blink on cinematic glow scenes
Composition make_text_glow_reveal_via_motion() {
    using namespace chronon3d::content::text;
    return typewriter::make_typewriter("TextGlowReveal", {
        typewriter::TypewriterLine(
            "A GLOWING TYPEWRITER LINE BLOOMS ON SCREEN with a soft halo effect that makes each character pulse gently as it appears — perfect for dramatic and atmospheric typography motion.")
            .set_pos({0.0f, 0.0f, 0.0f})
            .set_font(40.0f, 3.0f)
            .set_timing(0.0f, 1.8f)
            .set_color({0.90f, 0.92f, 1.0f, 1.0f})
            .set_align(TextAlign::Left)
            .set_size({1400.0f, 280.0f})
        // glow=false: GlowBloom preset already enables st.effects.glow_enabled
        // via the layer effect system — setting MotionObject.glow would double it.
    }, presets::motion::MotionPreset::GlowBloom, false,
       Color{0.01f, 0.012f, 0.022f, 1.0f}, 180, 1100.0f, 1920, 1080);
}

// Re-implementation of content::text::text_typewriter() for the regression
// guard: same typewriter pipeline path, no glow preset (FadeIn).  Mirrors
// the production composition exactly so the comparison is apples-to-apples.
Composition make_text_typewriter_via_motion() {
    using namespace chronon3d::content::text;
    return typewriter::make_typewriter("TextTypewriter", {
        typewriter::TypewriterLine(
            "THE ENGINE LEARNED TO SPEAK, typed frame by frame \u2014 a single line that wraps when it reaches the edge of the viewport so you can see the left-to-right alignment in action on multiple rows.")
            .set_pos({0.0f, 0.0f, 0.0f})
            .set_font(48.0f, 4.0f)
            .set_timing(0.0f, 2.5f)
            .set_color({0.25f, 0.58f, 1.0f, 1.0f})
            .set_align(TextAlign::Left)
            .set_size({1400.0f, 280.0f})
    }, presets::motion::MotionPreset::FadeIn, false,
       Color{0.01f, 0.012f, 0.022f, 1.0f}, 180, 1100.0f, 1920, 1080);
}

// Brightness sample of a framebuffer.  Two complementary metrics:
//   - near_white_pixels: RGB > 0.59 (i.e. > 150/255).  Catches the white
//     sharp text of TextGlowReveal specifically; should remain high even
//     when the glow buffer partially covers the text.
//   - bright_pixels: luma > 0.4.  Catches the blue text of TextTypewriter
//     and the white text of TextGlowReveal alike, so the ratio between
//     the two compositions is meaningful as a "glow vs no-glow" baseline.
struct BrightnessStats {
    int near_white_pixels{0};
    int bright_pixels{0};
    int total_sampled{0};
};

BrightnessStats sample_brightness(const Framebuffer& fb, int step = 2) {
    BrightnessStats s;
    const int w = fb.width();
    const int h = fb.height();
    for (int y = 0; y < h; y += step) {
        for (int x = 0; x < w; x += step) {
            const Color c = fb.get_pixel(x, y);
            ++s.total_sampled;
            if (c.r > 0.59f && c.g > 0.59f && c.b > 0.59f) {
                ++s.near_white_pixels;
            }
            const float luma = 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
            if (luma > 0.4f) {
                ++s.bright_pixels;
            }
        }
    }
    return s;
}

#undef verify_golden_or_create

} // namespace

TEST_CASE("GlowGolden: neon card radius ladder") {
    auto renderer = make_renderer();
    auto rendered = renderer.render_frame(make_neon_card(), 0);
    REQUIRE(rendered != nullptr);
    verify_glow_golden_or_create(*rendered, "neon_card_radius_ladder.png");
}

TEST_CASE("GlowGolden: text hero and caption") {
    auto renderer = make_renderer();
    auto rendered = renderer.render_frame(make_text_glow_scene(), 0);
    REQUIRE(rendered != nullptr);
    verify_glow_golden_or_create(*rendered, "text_hero_caption.png");
}

TEST_CASE("GlowGolden: image glow alpha coverage") {
    auto renderer = make_renderer();
    auto rendered = renderer.render_frame(make_image_glow_scene(), 0);
    REQUIRE(rendered != nullptr);
    verify_glow_golden_or_create(*rendered, "image_glow_alpha.png");
}

TEST_CASE("GlowGolden: edge clip safety") {
    auto renderer = make_renderer();
    auto rendered = renderer.render_frame(make_edge_glow_scene(), 0);
    REQUIRE(rendered != nullptr);
    verify_glow_golden_or_create(*rendered, "edge_clip_safety.png");
}

TEST_CASE("GlowGolden: pulse animation frame pair") {
    auto renderer = make_renderer();
    const auto comp = make_pulse_scene();

    auto fb0 = renderer.render_frame(comp, 0);
    auto fb15 = renderer.render_frame(comp, 15);
    REQUIRE(fb0 != nullptr);
    REQUIRE(fb15 != nullptr);

    verify_glow_golden_or_create(*fb0, "pulse_frame_000.png");
    verify_glow_golden_or_create(*fb15, "pulse_frame_015.png");

    // At t=0 and t=0.5 the pulse = 0.5 + 0.5*sin(2pi*2*t) gives 0.5 at both
    // frame 0 (t=0) and frame 15 (t=15/30=0.5 → sin(2pi)=0), so they are
    // identical.  Compare frame 4 (t=4/30≈0.133 → peak) and frame 11 instead.
    auto fb4 = renderer.render_frame(comp, 4);
    auto fb11 = renderer.render_frame(comp, 11);
    REQUIRE(fb4 != nullptr);
    REQUIRE(fb11 != nullptr);
    CHECK(framebuffer_hash(*fb4) != framebuffer_hash(*fb11));
}

// ── Motion-object / typewriter pipeline regression tests ──────────────────────
//
// These tests exercise the PRODUCTION path used by content::text::text_glow_reveal():
//   make_typewriter() → MotionObject::text() → motion_renderer →
//   GlowBloom preset → text_glow.cpp → draw_text_glow()
//
// Frame 90 is t=0.5 — past the reveal (text fully typed, opacity=1) and
// past the 30-frame blur limit (sharp text).  Catches:
//   - hardcoded per-layer glow strengths in text_glow.cpp
//   - double-glow routing (glow=true vs GlowBloom preset)
//   - sharp-text blur leaking past frame 30
//   - cursor blink on cinematic glow scenes
TEST_CASE("GlowGolden: TextGlowReveal via motion_object (typewriter pipeline, settled)") {
    auto renderer = make_renderer();
    const auto comp = make_text_glow_reveal_via_motion();
    auto rendered = renderer.render_frame(comp, 90);
    REQUIRE(rendered != nullptr);
    verify_glow_golden_or_create(*rendered, "text_glow_reveal_motion_frame_090.png");
}

TEST_CASE("GlowGolden: TextGlowReveal via motion_object (typewriter pipeline, early reveal)") {
    auto renderer = make_renderer();
    const auto comp = make_text_glow_reveal_via_motion();
    // Frame 30 = t=0.167 — exactly at the 30-frame blur cutoff.  Catches
    // regressions where the blur doesn't fade by frame 30 (sharp text).
    auto rendered = renderer.render_frame(comp, 30);
    REQUIRE(rendered != nullptr);
    verify_glow_golden_or_create(*rendered, "text_glow_reveal_motion_frame_030.png");
}

// Regression guard: TextGlowReveal must never fully hide the sharp text
// under the glow buffer.  Compares against the un-glowed TextTypewriter
// (same typewriter pipeline, FadeIn preset, glow=false) at frame 90
// (t=0.5 — text fully revealed, opacity=1, blur=0).
//
// Two assertions:
//   1. Absolute floor: TextGlowReveal has >= 1000 near-white pixels
//      (RGB > 0.59).  Catches the failure mode where the glow buffer
//      fully covers the text (e.g. if the per-layer strengths are
//      turned up past visibility, or the padding eats the sharp layer).
//   2. Relative floor: TextGlowReveal keeps at least 60% of the bright
//      (luma > 0.4) pixel count of TextTypewriter.  Catches the
//      regression where the glow overpowers the text but does not
//      fully hide it (e.g. weak sharp text, oversized glow buffer).
//
// The 60% threshold is generous on purpose: the glow legitimately
// reduces local contrast on the text by tinting it, but the
// character-coverage footprint should be comparable.
TEST_CASE("GlowGolden: TextGlowReveal must not hide text (regression guard vs TextTypewriter)") {
    auto renderer = make_renderer();
    const auto comp_glow = make_text_glow_reveal_via_motion();
    const auto comp_type = make_text_typewriter_via_motion();

    auto fb_glow = renderer.render_frame(comp_glow, 90);
    auto fb_type = renderer.render_frame(comp_type, 90);
    REQUIRE(fb_glow != nullptr);
    REQUIRE(fb_type != nullptr);

    const auto stats_glow = sample_brightness(*fb_glow);
    const auto stats_type = sample_brightness(*fb_type);

    INFO("TextGlowReveal[frame=90]:  near_white=" << stats_glow.near_white_pixels
         << ", bright=" << stats_glow.bright_pixels
         << ", total=" << stats_glow.total_sampled);
    INFO("TextTypewriter[frame=90]:  near_white=" << stats_type.near_white_pixels
         << ", bright=" << stats_type.bright_pixels
         << ", total=" << stats_type.total_sampled);

    // Guard 1: absolute floor on near-white pixels (catches full-hide).
    CHECK_MESSAGE(stats_glow.near_white_pixels >= 1000,
        "TextGlowReveal has only " << stats_glow.near_white_pixels
        << " near-white pixels at frame 90 — the glow is hiding the text");

    // Guard 2: bright-pixel ratio vs TextTypewriter (catches over-glow).
    if (stats_type.bright_pixels > 0) {
        const float ratio = static_cast<float>(stats_glow.bright_pixels) /
                            static_cast<float>(stats_type.bright_pixels);
        CHECK_MESSAGE(ratio >= 0.6f,
            "TextGlowReveal bright/text_bright ratio = " << ratio
            << " (glow=" << stats_glow.bright_pixels
            << ", text=" << stats_type.bright_pixels
            << ") — glow overpowers the text");
    }
}
