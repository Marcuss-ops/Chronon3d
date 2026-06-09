#include <doctest/doctest.h>

#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/media/media_placement.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/model/core/effect_stack.hpp>
#include <tests/helpers/test_utils.hpp>

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

        const float t = static_cast<float>(ctx.frame) / 30.0f;
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
