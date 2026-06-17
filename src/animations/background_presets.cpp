#include <chronon3d/api/backgrounds.hpp>
#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/utils/dark_grid_background.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <filesystem>

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

} // namespace

const BackgroundCatalog& builtin_background_catalog() { return builtin_background_catalog_storage(); }

const BackgroundPresetDescriptor* find_builtin_background(std::string_view id) {
    for (const auto& p : builtin_background_catalog().presets) if (p.id == id) return &p;
    return nullptr;
}

std::string builtin_background_catalog_json() {
    nlohmann::json j = nlohmann::json::array();
    for (const auto& p : builtin_background_catalog().presets)
        j.push_back({{"id",p.id},{"name",p.name},{"family",p.family},{"description",p.description},{"duration_seconds",p.duration_seconds},{"fps",p.fps},{"loopable",p.loopable},{"realtime_safe",p.realtime_safe}});
    return j.dump(2);
}

void render_builtin_background(SceneBuilder& s, const FrameContext& ctx, std::string_view id, const BackgroundOptions& opt) {
    if (id == "grid_clean") {
        auto path = scene::utils::detail::ensure_dark_grid_background_image(ctx.width, ctx.height, {
            .bg_color = opt.background, .grid_color = {1,1,1,0.9f}, .spacing = 160, .extent = 4000, .centered = true
        });
        s.screen_layer("grid_clean", [path, w=ctx.width, h=ctx.height](auto& l) {
            l.cache_static().image("img", {.path = path.string(), .size = {static_cast<f32>(w), static_cast<f32>(h)}});
        });
    }
}

} // namespace chronon3d::api

extern "C" const char* chronon3d_background_catalog_json() {
    static const std::string json = chronon3d::api::builtin_background_catalog_json();
    return json.c_str();
}

namespace chronon3d {

void register_background_presets_composition() {
    detail::add_builtin_composition("GridCleanBackground", []() {
        return composition({.name = "GridCleanBackground", .duration = 90}, [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            api::BackgroundOptions opt;
            opt.background = Color::black(); opt.accent = Color::white(); opt.glow = Color::black();
            api::render_builtin_background(s, ctx, "grid_clean", opt);
            return s.build();
        });
    });
}

} // namespace chronon3d
