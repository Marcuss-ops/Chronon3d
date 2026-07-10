#include <chronon3d/scene/builders/scene_builder.hpp>
#include <doctest/doctest.h>

#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/scene/model/core/effect_stack.hpp>
#include <tests/helpers/test_utils.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
using namespace chronon3d;

using namespace chronon3d::test;

namespace {

struct ComparisonResult {
    bool success{true};
    float max_channel_diff{0.0f};
    float mean_error{0.0f};
    float mismatch_percentage{0.0f};
    std::string error_message;
};

ComparisonResult compare_images(const Framebuffer& rendered, const Framebuffer& golden) {
    ComparisonResult res;
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
            const Color c1 = rendered.get_pixel(x, y).to_srgb();
            const Color c2 = golden.get_pixel(x, y);

            const float dr = std::abs(c1.r - c2.r);
            const float dg = std::abs(c1.g - c2.g);
            const float db = std::abs(c1.b - c2.b);
            const float da = std::abs(c1.a - c2.a);

            const float max_diff = std::max({dr, dg, db, da});
            res.max_channel_diff = std::max(res.max_channel_diff, max_diff);
            total_channel_diff += dr + dg + db + da;

            if (max_diff > 4.0f / 255.0f) {
                ++mismatched_pixels;
            }
        }
    }

    res.mean_error = static_cast<float>(total_channel_diff / (total_pixels * 4));
    res.mismatch_percentage = static_cast<float>(mismatched_pixels) / static_cast<float>(total_pixels);

    if (res.mismatch_percentage > 0.08f || res.mean_error >= 0.02f || res.max_channel_diff >= 100.0f / 255.0f) {
        res.success = false;
        res.error_message = "Threshold exceeded: maxPixelDiff=" + std::to_string(res.mismatch_percentage * 100.0f) + "%" +
                            ", meanError=" + std::to_string(res.mean_error) +
                            ", maxChannelError=" + std::to_string(res.max_channel_diff * 255.0f) + "/255";
    }

    return res;
}

bool framebuffer_has_only_finite(const Framebuffer& fb) {
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            const Color c = fb.get_pixel(x, y);
            if (!std::isfinite(c.r) || !std::isfinite(c.g) || !std::isfinite(c.b) || !std::isfinite(c.a)) {
                return false;
            }
        }
    }
    return true;
}

void verify_golden_or_create(const Framebuffer& rendered, const std::string& filename) {
    const std::filesystem::path golden_dir = "test_renders/golden/roadmap_2_5d";
    std::filesystem::create_directories(golden_dir);
    const std::filesystem::path golden_path = golden_dir / filename;

    if (!std::filesystem::exists(golden_path)) {
        REQUIRE(save_png(rendered, golden_path.string()));
    }

    auto golden = load_png_as_framebuffer(golden_path.string());
    REQUIRE(golden != nullptr);

    const auto comp = compare_images(rendered, *golden);
    INFO(comp.error_message);
    CHECK(comp.success);
}

std::shared_ptr<Framebuffer> render_frame(const Composition& comp, Frame frame) {
    SoftwareRenderer renderer = test::make_renderer();
    return renderer.render(comp, frame);
}

void save_debug_frame(const Framebuffer& fb, const std::string& filename) {
    const char* enable = std::getenv("CHRONON_SAVE_DEBUG_FRAMES");
    if (enable == nullptr || std::string_view(enable) != "1") {
        return;
    }
    const std::filesystem::path out = std::filesystem::path("output/debug/roadmap_2_5d") / filename;
    std::filesystem::create_directories(out.parent_path());
    REQUIRE(save_png(fb, out.string()));
}

Composition make_hero_push_scene() {
    return Composition({.width = 960, .height = 540, .duration = 45}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const float t = static_cast<float>(ctx.frame) / 44.0f;
        const float cam_z = -1200.0f + t * 350.0f;
        s.camera().enable(true).position({0.0f, 0.0f, cam_z}).zoom(1100.0f).look_at({0.0f, 0.0f, 0.0f});
        s.ambient_light(Color{1.0f, 1.0f, 1.0f, 1.0f}, 0.12f);
        s.directional_light({-0.25f, 1.0f, -0.65f}, Color{1.0f, 1.0f, 1.0f, 1.0f}, 0.88f);

        s.layer("bg", [](LayerBuilder& l) {
            l.grid_background("grid", GridBackgroundParams{
                .size = {960.0f, 540.0f},
                .bg_color = {0.01f, 0.01f, 0.03f, 1.0f},
                .grid_color = {0.28f, 0.48f, 0.98f, 0.08f},
                .spacing = 60.0f,
                .minor_thickness = 1.0f,
                .major_thickness = 2.0f,
                .major_every = 4,
                .centered = true
            });
        });

        s.layer("halo", [](LayerBuilder& l) {
            l.position({0.0f, -45.0f, 0.0f}).glow(GlowParams{.radius = 84.0f, .intensity = 1.0f, .color = Color{0.12f, 0.62f, 1.0f, 1.0f}});
            l.circle("c", {.radius = 220.0f, .color = Color{0.10f, 0.35f, 0.85f, 0.08f}});
        });

        s.layer("hero", [](LayerBuilder& l) {
            l.position({0.0f, -20.0f, 0.0f}).glow(GlowPresets::neon_blue(26.0f));
            l.text("hero", {
                .content = {.value = "SaaS"},
                .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                         .font_family = "Inter",
                         .font_size = 96.0f},
                .layout = {.box = {700.0f, 140.0f},
                           .align = TextAlign::Center,
                           .vertical_align = VerticalAlign::Middle},
                .appearance = {.color = Color{0.92f, 0.97f, 1.0f, 1.0f}},
                .placement = {TextPlacementKind::Absolute}, .offset = {0.0f, 0.0f}
            });
        });

        s.layer("sub", [](LayerBuilder& l) {
            l.position({0.0f, 110.0f, 0.0f});
            l.text("sub", {
                .content = {.value = "BUILD • LAUNCH • SCALE"},
                .font = {.font_path = "assets/fonts/Inter-Regular.ttf",
                         .font_family = "Inter",
                         .font_size = 22.0f},
                .layout = {.box = {720.0f, 42.0f},
                           .align = TextAlign::Center,
                           .vertical_align = VerticalAlign::Middle},
                .appearance = {.color = Color{0.72f, 0.88f, 1.0f, 1.0f}},
                .placement = {TextPlacementKind::Absolute}, .offset = {0.0f, 0.0f}
            });
        });

        s.layer("cta", [](LayerBuilder& l) {
            l.position({0.0f, 220.0f, 0.0f}).glow(GlowPresets::cinematic_gold(30.0f));
            l.rounded_rect("pill", {
                .size = {280.0f, 74.0f},
                .radius = 18.0f,
                .color = Color{0.99f, 0.48f, 0.32f, 1.0f}
            });
            l.text("cta_text", {
                .content = {.value = "PART 1"},
                .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                         .font_family = "Inter",
                         .font_size = 34.0f},
                .layout = {.box = {260.0f, 60.0f},
                           .align = TextAlign::Center,
                           .vertical_align = VerticalAlign::Middle},
                .appearance = {.color = Color::white()},
                .placement = {TextPlacementKind::Absolute}, .offset = {0.0f, 0.0f}
            });
        });

        return s.build();
    });
}

Composition make_buttery_card_scene(f32 rotation_y) {
    return Composition({.width = 960, .height = 540, .duration = 1}, [=](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera().enable(true).position({0.0f, 0.0f, -1000.0f}).zoom(1000.0f).look_at({0.0f, 0.0f, 0.0f});
        s.ambient_light(Color{1.0f, 1.0f, 1.0f, 1.0f}, 0.18f);
        s.directional_light({0.25f, 1.0f, 0.45f}, Color{1.0f, 1.0f, 1.0f, 1.0f}, 0.82f);

        s.layer("bg", [](LayerBuilder& l) {
            l.fill({0.04f, 0.01f, 0.07f, 1.0f});
        });

        s.layer("card", [=](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 0.0f, 0.0f}).rotate({0.0f, rotation_y * 0.7f, 0.0f});
            l.rect("plate", {
                .size = {760.0f, 380.0f},
                .color = Color{0.14f, 0.02f, 0.20f, 1.0f}
            });
        });

        s.layer("label", [](LayerBuilder& l) {
            l.position({0.0f, 0.0f, 0.0f});
            l.text("title", {
                .content = {.value = "Buttery Smooth"},
                .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                         .font_family = "Inter",
                         .font_size = 66.0f},
                .layout = {.box = {700.0f, 120.0f},
                           .align = TextAlign::Center,
                           .vertical_align = VerticalAlign::Middle},
                .appearance = {.color = Color{1.0f, 0.22f, 0.82f, 1.0f}},
                .placement = {TextPlacementKind::Absolute}, .offset = {0.0f, -10.0f}
            });
            l.text("subtitle", {
                .content = {.value = "Motion design quality text with soft glow"},
                .font = {.font_path = "assets/fonts/Inter-Regular.ttf",
                         .font_family = "Inter",
                         .font_size = 22.0f},
                .layout = {.box = {700.0f, 48.0f},
                           .align = TextAlign::Center,
                           .vertical_align = VerticalAlign::Middle},
                .appearance = {.color = Color{0.82f, 0.78f, 0.92f, 1.0f}},
                .placement = {TextPlacementKind::Absolute}, .offset = {0.0f, 100.0f}
            });
        });

        return s.build();
    });
}

Composition make_checker_perspective_scene() {
    return Composition({.width = 960, .height = 540, .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera().enable(true).position({0.0f, 0.0f, -1000.0f}).zoom(1000.0f).look_at({0.0f, 0.0f, 0.0f});
        s.ambient_light(Color{1.0f, 1.0f, 1.0f, 1.0f}, 0.08f);
        s.directional_light({-0.20f, 1.0f, -0.40f}, Color{1.0f, 1.0f, 1.0f, 1.0f}, 0.92f);

        s.layer("bg", [](LayerBuilder& l) {
            l.fill({0.01f, 0.02f, 0.05f, 1.0f});
        });

        s.layer("card", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 0.0f, 0.0f}).rotate({18.0f, 58.0f, 0.0f}).glow(GlowPresets::soft_cyan(36.0f));
            l.image("checker", {
                .path = "assets/images/checker.png",
                .size = {360.0f, 360.0f},
                .pos = {-180.0f, -180.0f, 0.0f},
                .fit = FitMode::Contain,
                .opacity = 1.0f,
                .radius = 20.0f
            });
        });

        return s.build();
    });
}

Composition make_zstack_scene(f32 camera_pan) {
    return Composition({.width = 960, .height = 540, .duration = 1}, [=](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera().enable(true).position({camera_pan, 0.0f, -1000.0f}).zoom(1000.0f).look_at({0.0f, 0.0f, 0.0f});
        s.ambient_light(Color{1.0f, 1.0f, 1.0f, 1.0f}, 0.14f);
        s.directional_light({-0.40f, 1.0f, -0.75f}, Color{1.0f, 1.0f, 1.0f, 1.0f}, 0.86f);

        s.layer("bg", [](LayerBuilder& l) {
            l.fill({0.02f, 0.02f, 0.06f, 1.0f});
        });

        s.layer("back", [](LayerBuilder& l) {
            l.enable_3d().position({-220.0f, 0.0f, 520.0f});
            l.rect("card", {.size = {220.0f, 260.0f}, .color = Color{0.14f, 0.24f, 0.64f, 0.98f}});
        });

        s.layer("mid", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 0.0f, 120.0f}).rotate({0.0f, -4.0f, 0.0f});
            l.rect("card", {.size = {240.0f, 280.0f}, .color = Color{0.20f, 0.18f, 0.60f, 0.98f}});
        });

        s.layer("front", [](LayerBuilder& l) {
            l.enable_3d().position({220.0f, 0.0f, -180.0f}).rotate({0.0f, 12.0f, 0.0f});
            l.rect("card", {.size = {240.0f, 280.0f}, .color = Color{0.96f, 0.28f, 0.22f, 0.98f}});
        });

        return s.build();
    });
}

Composition make_shadow_receiver_scene(float height) {
    return Composition({.width = 960, .height = 540, .duration = 1}, [=](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera().enable(true).position({0.0f, 0.0f, -1000.0f}).zoom(1000.0f).look_at({0.0f, 0.0f, 0.0f});
        s.ambient_light(Color{1.0f, 1.0f, 1.0f, 1.0f}, 0.12f);
        s.directional_light({-0.55f, 1.0f, -0.35f}, Color{1.0f, 1.0f, 1.0f, 1.0f}, 0.92f);

        s.layer("floor", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 120.0f, 240.0f}).accepts_shadows(true);
            l.rect("floor_rect", {.size = {860.0f, 200.0f}, .color = Color{0.84f, 0.86f, 0.90f, 1.0f}});
        });

        s.layer("box", [=](LayerBuilder& l) {
            l.enable_3d().position({0.0f, -70.0f, height}).casts_shadows(true);
            l.rounded_rect("box_rect", {.size = {140.0f, 100.0f}, .radius = 18.0f, .color = Color{0.98f, 0.22f, 0.18f, 1.0f}});
        });

        return s.build();
    });
}

Composition make_rim_light_scene() {
    return Composition({.width = 960, .height = 540, .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera().enable(true).position({0.0f, 0.0f, -1000.0f}).zoom(1000.0f).look_at({0.0f, 0.0f, 0.0f});
        s.ambient_light(Color{1.0f, 1.0f, 1.0f, 1.0f}, 0.08f);
        s.directional_light({0.10f, 0.15f, 1.0f}, Color{0.20f, 0.88f, 1.0f, 1.0f}, 1.0f);

        s.layer("bg", [](LayerBuilder& l) {
            l.fill({0.02f, 0.02f, 0.06f, 1.0f});
        });

        s.layer("card", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 0.0f, 0.0f}).rotate({0.0f, 35.0f, 0.0f});
            l.rounded_rect("plate", {.size = {480.0f, 260.0f}, .radius = 26.0f, .color = Color{0.06f, 0.06f, 0.10f, 1.0f}});
        });

        return s.build();
    });
}

Composition make_depth_fog_scene() {
    return Composition({.width = 960, .height = 540, .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera().enable(true).position({0.0f, 0.0f, -1100.0f}).zoom(1000.0f).look_at({0.0f, 0.0f, 0.0f});
        s.camera().dof(DepthOfFieldSettings{.enabled = true, .focus_z = 650.0f, .aperture = 0.018f, .max_blur = 18.0f});
        s.ambient_light(Color{1.0f, 1.0f, 1.0f, 1.0f}, 0.10f);
        s.directional_light({0.0f, 1.0f, -1.0f}, Color{0.35f, 0.72f, 1.0f, 1.0f}, 0.92f);

        s.layer("bg", [](LayerBuilder& l) {
            l.fill({0.01f, 0.02f, 0.05f, 1.0f});
        });

        const std::array<std::pair<const char*, float>, 5> items{{
            {"FRONT", 140.0f},
            {"BUILD", 360.0f},
            {"CREATE", 620.0f},
            {"FUTURE", 900.0f},
            {"BACKGROUND", 1220.0f},
        }};

        for (const auto& [label, z] : items) {
            s.layer(label, [=](LayerBuilder& l) {
                l.enable_3d().position({0.0f, 0.0f, z}).opacity(std::clamp(1.0f - z / 1800.0f, 0.22f, 1.0f));
                l.text(label, {
                    .content = {.value = label},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_size = 62.0f},
                    .layout = {.box = {800.0f, 110.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color{0.62f, 0.82f, 1.0f, 1.0f}},
                    .placement = {TextPlacementKind::Absolute}, .offset = {0.0f, 0.0f}
                });
            });
        }

        return s.build();
    });
}

Composition make_orbit_stability_scene(float yaw) {
    return Composition({.width = 960, .height = 540, .duration = 1}, [=](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera().enable(true).position({0.0f, 0.0f, -1050.0f}).zoom(1000.0f).look_at({0.0f, 0.0f, 0.0f});
        s.camera().pan(yaw);
        s.ambient_light(Color{1.0f, 1.0f, 1.0f, 1.0f}, 0.14f);
        s.directional_light({-0.25f, 1.0f, -0.75f}, Color{1.0f, 1.0f, 1.0f, 1.0f}, 0.86f);

        s.layer("bg", [](LayerBuilder& l) {
            l.grid_background("grid_bg", GridBackgroundParams{
                .size = {960.0f, 540.0f},
                .bg_color = {0.015f, 0.02f, 0.045f, 1.0f},
                .grid_color = {0.22f, 0.32f, 0.85f, 0.08f},
                .spacing = 64.0f,
                .minor_thickness = 1.0f,
                .major_thickness = 2.0f,
                .major_every = 4,
                .centered = true
            });
        });

        s.layer("left", [](LayerBuilder& l) {
            l.enable_3d().position({-250.0f, -12.0f, 0.0f}).rotate({0.0f, -20.0f, 0.0f});
            l.rounded_rect("card", {.size = {180.0f, 220.0f}, .radius = 20.0f, .color = Color{0.16f, 0.62f, 0.98f, 1.0f}});
        });

        s.layer("center", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 0.0f, 90.0f});
            l.rounded_rect("card", {.size = {220.0f, 260.0f}, .radius = 24.0f, .color = Color{0.82f, 0.24f, 0.96f, 1.0f}});
            l.text("title", {
                .content = {.value = "ORBIT"},
                .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                         .font_family = "Inter",
                         .font_size = 48.0f},
                .layout = {.box = {220.0f, 80.0f},
                           .align = TextAlign::Center,
                           .vertical_align = VerticalAlign::Middle},
                .appearance = {.color = Color::white()},
                .placement = {TextPlacementKind::Absolute}, .offset = {0.0f, -6.0f}
            });
        });

        s.layer("right", [](LayerBuilder& l) {
            l.enable_3d().position({250.0f, 12.0f, -160.0f}).rotate({0.0f, 22.0f, 0.0f});
            l.rounded_rect("card", {.size = {180.0f, 220.0f}, .radius = 20.0f, .color = Color{0.12f, 0.95f, 0.78f, 1.0f}});
        });

        return s.build();
    });
}

Composition make_fake_extrusion_scene() {
    return Composition({.width = 960, .height = 540, .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera().enable(true).position({0.0f, 0.0f, -1000.0f}).zoom(1000.0f).look_at({0.0f, 0.0f, 0.0f});
        s.ambient_light(Color{1.0f, 1.0f, 1.0f, 1.0f}, 0.12f);
        s.directional_light({0.20f, 1.0f, 0.55f}, Color{1.0f, 1.0f, 1.0f, 1.0f}, 0.88f);

        s.layer("bg", [](LayerBuilder& l) {
            l.fill({0.02f, 0.02f, 0.05f, 1.0f});
        });

        s.layer("extrude_back", [](LayerBuilder& l) {
            l.position({10.0f, 12.0f, -20.0f});
            l.text("back", {
                .content = {.value = "SaaS"},
                .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                         .font_family = "Inter",
                         .font_size = 100.0f},
                .layout = {.box = {620.0f, 140.0f},
                           .align = TextAlign::Center,
                           .vertical_align = VerticalAlign::Middle},
                .appearance = {.color = Color{0.10f, 0.18f, 0.42f, 1.0f}},
                .placement = {TextPlacementKind::Absolute}, .offset = {0.0f, 0.0f}
            });
        });

        s.layer("extrude_front", [](LayerBuilder& l) {
            l.position({0.0f, 0.0f, 0.0f}).glow(GlowPresets::soft_cyan(20.0f));
            l.text("front", {
                .content = {.value = "SaaS"},
                .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                         .font_family = "Inter",
                         .font_size = 100.0f},
                .layout = {.box = {620.0f, 140.0f},
                           .align = TextAlign::Center,
                           .vertical_align = VerticalAlign::Middle},
                .appearance = {.color = Color::white()},
                .placement = {TextPlacementKind::Absolute}, .offset = {0.0f, 0.0f}
            });
        });

        return s.build();
    });
}

Composition make_artifact_torture_scene() {
    return Composition({.width = 960, .height = 540, .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera().enable(true).position({0.0f, 0.0f, -1000.0f}).zoom(1000.0f).look_at({0.0f, 0.0f, 0.0f});
        s.ambient_light(Color{1.0f, 1.0f, 1.0f, 1.0f}, 0.10f);
        s.directional_light({-0.35f, 1.0f, -0.75f}, Color{1.0f, 1.0f, 1.0f, 1.0f}, 0.90f);

        s.layer("bg", [](LayerBuilder& l) {
            l.fill({0.01f, 0.01f, 0.02f, 1.0f});
        });

        for (int i = 0; i < 10; ++i) {
            const float px = -420.0f + static_cast<float>(i) * 92.0f;
            const float py = -140.0f + static_cast<float>((i % 3) * 60.0f);
            const float rot = static_cast<float>((i % 4) * 8);
            s.layer("card_" + std::to_string(i), [=](LayerBuilder& l) {
                l.enable_3d().position({px, py, static_cast<float>(i) * 35.0f}).rotate({0.0f, rot, 0.0f});
                l.rounded_rect("r", {
                    .size = {130.0f, 96.0f},
                    .radius = 16.0f,
                    .color = Color{0.18f + 0.05f * i, 0.12f, 0.42f + 0.04f * i, 0.92f}
                });
                l.glow(GlowParams{.radius = 18.0f + static_cast<float>(i) * 2.0f, .intensity = 0.5f + 0.05f * i, .color = Color{0.38f, 0.15f + 0.05f * i, 1.0f, 1.0f}});
            });
        }

        return s.build();
    });
}

} // namespace

TEST_CASE("GRAPHIC TEST 01 - Hero Push-In") {
    auto comp = make_hero_push_scene();
    auto fb_start = render_frame(comp, 0);
    auto fb_mid   = render_frame(comp, 22);
    auto fb_end   = render_frame(comp, 44);
    REQUIRE(fb_start != nullptr);
    REQUIRE(fb_mid != nullptr);
    REQUIRE(fb_end != nullptr);
    save_debug_frame(*fb_start, "01_hero_push_start.png");
    save_debug_frame(*fb_mid, "01_hero_push_mid.png");
    save_debug_frame(*fb_end, "01_hero_push_end.png");
    verify_golden_or_create(*fb_mid, "01_hero_push_mid.png");
}

TEST_CASE("GRAPHIC TEST 02 - Y Rotation Card") {
    auto comp = make_buttery_card_scene(35.0f);
    auto fb = render_frame(comp, 0);
    REQUIRE(fb != nullptr);
    CHECK(framebuffer_has_only_finite(*fb));
}

TEST_CASE("GRAPHIC TEST 03 - Extreme Perspective Checkerboard") {
    auto comp = make_checker_perspective_scene();
    auto fb = render_frame(comp, 0);
    REQUIRE(fb != nullptr);
    save_debug_frame(*fb, "03_checkerboard_perspective.png");
    verify_golden_or_create(*fb, "03_checkerboard_perspective.png");
}

TEST_CASE("GRAPHIC TEST 04 - Z Stack Occlusion") {
    auto comp = make_zstack_scene(-120.0f);
    auto fb = render_frame(comp, 0);
    REQUIRE(fb != nullptr);
    save_debug_frame(*fb, "04_z_stack_occlusion.png");
    verify_golden_or_create(*fb, "04_z_stack_occlusion.png");
}

TEST_CASE("GRAPHIC TEST 05 - Cast Shadow Receiver") {
    auto fb_low  = render_frame(make_shadow_receiver_scene(50.0f), 0);
    auto fb_mid  = render_frame(make_shadow_receiver_scene(200.0f), 0);
    auto fb_high = render_frame(make_shadow_receiver_scene(500.0f), 0);
    REQUIRE(fb_low != nullptr);
    REQUIRE(fb_mid != nullptr);
    REQUIRE(fb_high != nullptr);
    save_debug_frame(*fb_low, "05_shadow_height_low.png");
    save_debug_frame(*fb_mid, "05_shadow_height_mid.png");
    save_debug_frame(*fb_high, "05_shadow_height_high.png");
    verify_golden_or_create(*fb_mid, "05_shadow_height_mid.png");
}

TEST_CASE("GRAPHIC TEST 06 - Rim Light Material") {
    auto comp = make_rim_light_scene();
    auto fb = render_frame(comp, 0);
    REQUIRE(fb != nullptr);
    CHECK(framebuffer_has_only_finite(*fb));
}

TEST_CASE("GRAPHIC TEST 07 - Depth Fog Tunnel") {
    auto comp = make_depth_fog_scene();
    auto fb = render_frame(comp, 0);
    REQUIRE(fb != nullptr);
    save_debug_frame(*fb, "07_depth_fog_tunnel.png");
    verify_golden_or_create(*fb, "07_depth_fog_tunnel.png");
}

TEST_CASE("GRAPHIC TEST 08 - Orbit Camera Stability") {
    auto comp_start = make_orbit_stability_scene(-15.0f);
    auto comp_mid   = make_orbit_stability_scene(0.0f);
    auto comp_end   = make_orbit_stability_scene(15.0f);
    auto fb_start = render_frame(comp_start, 0);
    auto fb_mid   = render_frame(comp_mid, 0);
    auto fb_end   = render_frame(comp_end, 0);
    REQUIRE(fb_start != nullptr);
    REQUIRE(fb_mid != nullptr);
    REQUIRE(fb_end != nullptr);
    save_debug_frame(*fb_start, "08_orbit_start.png");
    save_debug_frame(*fb_mid, "08_orbit_mid.png");
    save_debug_frame(*fb_end, "08_orbit_end.png");
    verify_golden_or_create(*fb_mid, "08_orbit_mid.png");
}

TEST_CASE("GRAPHIC TEST 09 - Fake Text Extrusion") {
    auto comp = make_fake_extrusion_scene();
    auto fb = render_frame(comp, 0);
    REQUIRE(fb != nullptr);
    CHECK(framebuffer_has_only_finite(*fb));
}

TEST_CASE("GRAPHIC TEST 10 - Artifact Torture Test") {
    auto comp = make_artifact_torture_scene();
    auto fb = render_frame(comp, 0);
    REQUIRE(fb != nullptr);
    CHECK(framebuffer_has_only_finite(*fb));
}
