#include <chronon3d/api/backgrounds.hpp>

#include <chronon3d/core/composition_registration.hpp>
#include <chronon3d/core/frame_context.hpp>
#include <chronon3d/math/constants.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace chronon3d::api {

namespace {

using Preset = BackgroundPresetDescriptor;

const std::array<Preset, 4> kBuiltinBackgrounds = {{
    {
        .id = "easy_aurora",
        .name = "Easy Aurora",
        .family = "background",
        .description = "A soft glow field with a couple of drifting color blobs.",
        .duration_seconds = 3.0f,
        .fps = 30,
        .loopable = true,
        .realtime_safe = true,
    },
    {
        .id = "easy_grid",
        .name = "Easy Grid",
        .family = "background",
        .description = "A minimal grid with subtle parallax drift and a single glow.",
        .duration_seconds = 3.0f,
        .fps = 30,
        .loopable = true,
        .realtime_safe = true,
    },
    {
        .id = "easy_ribbon",
        .name = "Easy Ribbon",
        .family = "background",
        .description = "Thin vertical ribbons moving in a soft repeating wave.",
        .duration_seconds = 3.0f,
        .fps = 30,
        .loopable = true,
        .realtime_safe = true,
    },
    {
        .id = "easy_orbit",
        .name = "Easy Orbit",
        .family = "background",
        .description = "A very small set of orbs orbiting over a clean studio plate.",
        .duration_seconds = 3.0f,
        .fps = 30,
        .loopable = true,
        .realtime_safe = true,
    },
}};

const BackgroundCatalog& builtin_background_catalog_storage() {
    static const BackgroundCatalog catalog{std::vector<Preset>(kBuiltinBackgrounds.begin(), kBuiltinBackgrounds.end())};
    return catalog;
}

void render_easy_aurora(SceneBuilder& s, const FrameContext& ctx, const BackgroundOptions& opt) {
    const float fps = std::max(1.0f, ctx.fps());
    const float time = static_cast<float>(ctx.effective_frame()) / fps;

    s.camera().set({
        .enabled = true,
        .position = {0.0f, 0.0f, -1000.0f},
        .point_of_interest = {0.0f, 0.0f, 0.0f},
        .point_of_interest_enabled = true,
        .zoom = 1000.0f,
    });

    s.layer("easy_aurora_base", [opt](LayerBuilder& l) {
        l.cache_static();
        l.fullscreen_rect("bg", opt.background);
    });

    s.layer("easy_aurora_glow", [opt, time](LayerBuilder& l) {
        l.enable_3d();
        l.position({0.0f, 0.0f, 200.0f});

        const float drift_x = std::sin(time * 0.35f) * 160.0f;
        const float drift_y = std::cos(time * 0.28f) * 80.0f;

        l.circle("aurora_left", CircleParams{
            .radius = 260.0f * opt.intensity,
            .color = opt.glow,
            .pos = {-320.0f + drift_x, -60.0f + drift_y, 0.0f}
        }).blur(64.0f);

        l.circle("aurora_right", CircleParams{
            .radius = 220.0f * opt.intensity,
            .color = Color{opt.accent.r, opt.accent.g, opt.accent.b, 0.18f},
            .pos = {340.0f - drift_x * 0.6f, 100.0f - drift_y * 0.5f, 0.0f}
        }).blur(52.0f);
    });
}

void render_easy_grid(SceneBuilder& s, const FrameContext& ctx, const BackgroundOptions& opt) {
    const float fps = std::max(1.0f, ctx.fps());
    const float time = static_cast<float>(ctx.effective_frame()) / fps;

    s.camera().set({
        .enabled = true,
        .position = {std::sin(time * 0.22f) * 20.0f, std::cos(time * 0.18f) * 12.0f, -1000.0f},
        .point_of_interest = {0.0f, 0.0f, 0.0f},
        .point_of_interest_enabled = true,
        .zoom = 1000.0f,
    });

    s.layer("easy_grid_base", [opt](LayerBuilder& l) {
        l.cache_static();
        l.fullscreen_rect("bg", opt.background);
    });

    s.layer("easy_grid", [opt, time](LayerBuilder& l) {
        l.enable_3d();
        l.position({0.0f, 0.0f, 250.0f});

        for (int i = -8; i <= 8; ++i) {
            const float x = static_cast<float>(i) * 140.0f;
            l.line("grid_v_" + std::to_string(i), LineParams{
                .from = {x, -620.0f, 0.0f},
                .to = {x, 620.0f, 0.0f},
                .thickness = 1.0f,
                .color = Color{opt.accent.r, opt.accent.g, opt.accent.b, 0.05f}
            });
        }

        for (int i = -4; i <= 4; ++i) {
            const float y = static_cast<float>(i) * 140.0f;
            l.line("grid_h_" + std::to_string(i), LineParams{
                .from = {-960.0f, y, 0.0f},
                .to = {960.0f, y, 0.0f},
                .thickness = 1.0f,
                .color = Color{opt.accent.r, opt.accent.g, opt.accent.b, 0.05f}
            });
        }

        const float glow_x = std::sin(time * 0.6f) * 220.0f;
        const float glow_y = std::cos(time * 0.4f) * 120.0f;
        l.circle("grid_glow", CircleParams{
            .radius = 200.0f * opt.intensity,
            .color = Color{opt.glow.r, opt.glow.g, opt.glow.b, 0.30f},
            .pos = {glow_x, glow_y, 0.0f}
        }).blur(84.0f);
    });
}

void render_easy_ribbon(SceneBuilder& s, const FrameContext& ctx, const BackgroundOptions& opt) {
    const float fps = std::max(1.0f, ctx.fps());
    const float time = static_cast<float>(ctx.effective_frame()) / fps;

    s.camera().set({
        .enabled = true,
        .position = {0.0f, 0.0f, -1000.0f},
        .point_of_interest = {0.0f, 0.0f, 0.0f},
        .point_of_interest_enabled = true,
        .zoom = 1000.0f,
    });

    s.layer("easy_ribbon_base", [opt](LayerBuilder& l) {
        l.cache_static();
        l.fullscreen_rect("bg", opt.background);
    });

    s.layer("easy_ribbon", [opt, time](LayerBuilder& l) {
        l.enable_3d();
        l.position({0.0f, 0.0f, 150.0f});

        for (int i = 0; i < 12; ++i) {
            const float seed = static_cast<float>(i);
            const float x = -900.0f + seed * 160.0f;
            const float y = std::sin(time * 0.9f + seed * 0.3f) * 240.0f;
            const float h = 180.0f + std::abs(std::sin(seed * 1.3f)) * 180.0f;
            const float a = 0.06f + std::abs(std::sin(seed * 0.7f)) * 0.14f;
            Color c = opt.accent;
            c.a = a;
            l.rect("ribbon_" + std::to_string(i), RectParams{
                .size = {24.0f, h},
                .color = c,
                .pos = {x, y, 0.0f}
            });
        }
    });
}

void render_easy_orbit(SceneBuilder& s, const FrameContext& ctx, const BackgroundOptions& opt) {
    const float fps = std::max(1.0f, ctx.fps());
    const float time = static_cast<float>(ctx.effective_frame()) / fps;

    s.camera().set({
        .enabled = true,
        .position = {std::sin(time * 0.18f) * 25.0f, std::cos(time * 0.14f) * 10.0f, -1000.0f},
        .point_of_interest = {0.0f, 0.0f, 0.0f},
        .point_of_interest_enabled = true,
        .zoom = 1000.0f,
    });

    s.layer("easy_orbit_base", [opt](LayerBuilder& l) {
        l.cache_static();
        l.fullscreen_rect("bg", opt.background);
    });

    s.layer("easy_orbit", [opt, time](LayerBuilder& l) {
        l.enable_3d();
        l.position({0.0f, 0.0f, 220.0f});

        for (int i = 0; i < 3; ++i) {
            const float phase = static_cast<float>(i) * 2.0943951f;
            const float orbit = time * (0.55f + 0.12f * static_cast<float>(i));
            const float x = std::sin(orbit + phase) * (320.0f + 30.0f * static_cast<float>(i));
            const float y = std::cos(orbit * 0.8f + phase) * (160.0f + 16.0f * static_cast<float>(i));
            const float r = (120.0f + 30.0f * static_cast<float>(i)) * opt.intensity;
            Color c = opt.glow;
            c.a = 0.2f + 0.08f * static_cast<float>(i);
            l.circle("orbit_" + std::to_string(i), CircleParams{
                .radius = r,
                .color = c,
                .pos = {x, y, 0.0f}
            }).blur(40.0f + 8.0f * static_cast<float>(i));
        }
    });
}

void render_builtin_background(
    SceneBuilder& s,
    const FrameContext& ctx,
    std::string_view id,
    const BackgroundOptions& opt)
{
    if (id == "easy_aurora") {
        render_easy_aurora(s, ctx, opt);
        return;
    }
    if (id == "easy_grid") {
        render_easy_grid(s, ctx, opt);
        return;
    }
    if (id == "easy_ribbon") {
        render_easy_ribbon(s, ctx, opt);
        return;
    }
    if (id == "easy_orbit") {
        render_easy_orbit(s, ctx, opt);
        return;
    }
    render_easy_aurora(s, ctx, opt);
}

} // namespace

const BackgroundCatalog& builtin_background_catalog() {
    return builtin_background_catalog_storage();
}

const BackgroundPresetDescriptor* find_builtin_background(std::string_view id) {
    const auto& presets = builtin_background_catalog().presets;
    const auto it = std::find_if(presets.begin(), presets.end(), [id](const Preset& preset) {
        return preset.id == id;
    });
    return it == presets.end() ? nullptr : &*it;
}

std::string builtin_background_catalog_json() {
    nlohmann::json root = nlohmann::json::array();
    for (const auto& preset : builtin_background_catalog().presets) {
        root.push_back({
            {"id", preset.id},
            {"name", preset.name},
            {"family", preset.family},
            {"description", preset.description},
            {"duration_seconds", preset.duration_seconds},
            {"fps", preset.fps},
            {"loopable", preset.loopable},
            {"realtime_safe", preset.realtime_safe},
        });
    }
    return root.dump(2);
}

} // namespace chronon3d::api

extern "C" const char* chronon3d_background_catalog_json() {
    static const std::string json = chronon3d::api::builtin_background_catalog_json();
    return json.c_str();
}

namespace {

using namespace chronon3d;

Composition easy_aurora_background() {
    return composition({
        .name = "EasyAuroraBackground",
        .width = 1920,
        .height = 1080,
        .duration = 90
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        api::BackgroundOptions opt;
        opt.background = Color{0.01f, 0.015f, 0.03f, 1.0f};
        opt.accent = Color{0.25f, 0.45f, 1.0f, 1.0f};
        opt.glow = Color{0.16f, 0.38f, 1.0f, 0.25f};
        api::render_builtin_background(s, ctx, "easy_aurora", opt);
        return s.build();
    });
}

Composition easy_grid_background() {
    return composition({
        .name = "EasyGridBackground",
        .width = 1920,
        .height = 1080,
        .duration = 90
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        api::BackgroundOptions opt;
        opt.background = Color{0.008f, 0.010f, 0.022f, 1.0f};
        opt.accent = Color{0.25f, 0.52f, 1.0f, 1.0f};
        opt.glow = Color{0.20f, 0.45f, 1.0f, 0.22f};
        api::render_builtin_background(s, ctx, "easy_grid", opt);
        return s.build();
    });
}

Composition easy_ribbon_background() {
    return composition({
        .name = "EasyRibbonBackground",
        .width = 1920,
        .height = 1080,
        .duration = 90
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        api::BackgroundOptions opt;
        opt.background = Color{0.012f, 0.009f, 0.018f, 1.0f};
        opt.accent = Color{0.76f, 0.36f, 1.0f, 1.0f};
        opt.glow = Color{0.62f, 0.28f, 1.0f, 0.18f};
        api::render_builtin_background(s, ctx, "easy_ribbon", opt);
        return s.build();
    });
}

Composition easy_orbit_background() {
    return composition({
        .name = "EasyOrbitBackground",
        .width = 1920,
        .height = 1080,
        .duration = 90
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        api::BackgroundOptions opt;
        opt.background = Color{0.010f, 0.012f, 0.024f, 1.0f};
        opt.accent = Color{0.36f, 0.64f, 1.0f, 1.0f};
        opt.glow = Color{0.22f, 0.42f, 1.0f, 0.22f};
        api::render_builtin_background(s, ctx, "easy_orbit", opt);
        return s.build();
    });
}

} // namespace

CHRONON_REGISTER_COMPOSITION("EasyAuroraBackground", easy_aurora_background)
CHRONON_REGISTER_COMPOSITION("EasyGridBackground", easy_grid_background)
CHRONON_REGISTER_COMPOSITION("EasyRibbonBackground", easy_ribbon_background)
CHRONON_REGISTER_COMPOSITION("EasyOrbitBackground", easy_orbit_background)
