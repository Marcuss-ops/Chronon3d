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

const std::array<Preset, 1> kBuiltinBackgrounds = {{
    {
        .id = "grid_clean",
        .name = "Grid Clean",
        .family = "background",
        .description = "A clean full-screen grid background.",
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

void render_grid_clean(SceneBuilder& s, const FrameContext&, const BackgroundOptions& opt) {
    s.camera().set({
        .enabled = true,
        .position = {0.0f, 0.0f, -1000.0f},
        .point_of_interest = {0.0f, 0.0f, 0.0f},
        .point_of_interest_enabled = true,
        .zoom = 1000.0f,
    });

    s.layer("grid_clean_base", [opt](LayerBuilder& l) {
        l.cache_static();
        l.fullscreen_rect("bg", opt.background);
    });

    s.layer("grid_clean_lines", [opt](LayerBuilder& l) {
        l.cache_static();

        // 1920x1080 grid spanning the entire screen.
        // We draw vertical lines every 140 pixels.
        for (int i = -7; i <= 7; ++i) {
            const float x = static_cast<float>(i) * 140.0f;
            l.line("grid_v_" + std::to_string(i), LineParams{
                .from = {x, -540.0f, 0.0f},
                .to = {x, 540.0f, 0.0f},
                .thickness = 1.0f,
                .color = opt.accent
            });
        }

        // Horizontal lines every 140 pixels.
        for (int i = -4; i <= 4; ++i) {
            const float y = static_cast<float>(i) * 140.0f;
            l.line("grid_h_" + std::to_string(i), LineParams{
                .from = {-960.0f, y, 0.0f},
                .to = {960.0f, y, 0.0f},
                .thickness = 1.0f,
                .color = opt.accent
            });
        }
    });
}

void render_builtin_background(
    SceneBuilder& s,
    const FrameContext& ctx,
    std::string_view id,
    const BackgroundOptions& opt)
{
    (void)id;
    render_grid_clean(s, ctx, opt);
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

Composition grid_clean_background() {
    return composition({
        .name = "GridCleanBackground",
        .width = 1920,
        .height = 1080,
        .duration = 90
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        api::BackgroundOptions opt;
        opt.background = Color{0.008f, 0.010f, 0.022f, 1.0f};
        opt.accent = Color{0.25f, 0.52f, 1.0f, 0.05f}; // subtle blue grid lines
        opt.glow = Color{0.0f, 0.0f, 0.0f, 0.0f};
        api::render_builtin_background(s, ctx, "grid_clean", opt);
        return s.build();
    });
}

} // namespace

CHRONON_REGISTER_COMPOSITION("GridCleanBackground", grid_clean_background)
