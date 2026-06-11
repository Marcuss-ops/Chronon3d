#include <chronon3d/api/backgrounds.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <glm/gtc/constants.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/utils/dark_grid_background.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
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

struct GridRenderer {
    static inline f32 dist(f32 v, f32 s) { return s <= 1e-6f ? 0.0f : std::abs(v - std::round(v / s) * s); }

    static void rasterize(Framebuffer& fb, const BackgroundOptions& opt) {
        const i32 W = fb.width(), H = fb.height();
        const f32 hw = W * 0.5f, hh = H * 0.5f;
        const Color bg = opt.background.to_linear();
        const Color glow = opt.glow.with_alpha(std::clamp(opt.intensity * 0.10f, 0.0f, 0.18f)).to_linear();
        const Color line = opt.accent.with_alpha(std::clamp(opt.accent.a * opt.intensity, 0.72f, 0.95f)).to_linear();
        const Color major = Color{line.r, line.g, line.b, 1.0f};

        fb.clear(bg);
        for (i32 y = 0; y < H; ++y) {
            f32 gy = y - hh;
            f32 mdy = dist(gy, 160.0f), Mdy = dist(gy, 640.0f);
            for (i32 x = 0; x < W; ++x) {
                f32 gx = x - hw;
                f32 mdx = dist(gx, 160.0f), Mdx = dist(gx, 640.0f);
                if (mdx <= 8.0f || mdy <= 8.0f) fb.set_pixel(x, y, compositor::blend_normal(glow, fb.get_pixel(x, y)));
                if (mdx <= 2.5f || mdy <= 2.5f) fb.set_pixel(x, y, compositor::blend_normal(line, fb.get_pixel(x, y)));
                if (mdx <= 5.5f || mdy <= 5.5f || Mdx <= 5.5f || Mdy <= 5.5f) fb.set_pixel(x, y, compositor::blend_normal(major, fb.get_pixel(x, y)));
            }
        }
    }
};

std::filesystem::path ensure_cached_grid(i32 w, i32 h, const BackgroundOptions& opt) {
    auto dir = std::filesystem::path("output") / "cache" / "grid_clean";
    std::filesystem::create_directories(dir);
    auto path = dir / (std::to_string(w) + "x" + std::to_string(h) + ".png");
    if (!std::filesystem::exists(path)) {
        Framebuffer fb(w, h);
        GridRenderer::rasterize(fb, opt);
        save_png(fb, path.string());
    }
    return path;
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
