#include <chronon3d/api/backgrounds.hpp>

#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/constants.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>
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

inline f32 distance_to_grid_line(f32 value, f32 step) {
    if (step <= 1e-6f) return 0.0f;
    return std::abs(value - std::round(value / step) * step);
}

std::filesystem::path grid_clean_cache_path(i32 width, i32 height) {
    std::filesystem::path dir = std::filesystem::path("output") / "cache" / "grid_clean_background";
    std::filesystem::create_directories(dir);
    return dir / (std::to_string(width) + "x" + std::to_string(height) + ".png");
}

void rasterize_grid_clean_background(Framebuffer& fb, const BackgroundOptions& opt) {
    const i32 W = fb.width();
    const i32 H = fb.height();
    const f32 half_w = static_cast<f32>(W) * 0.5f;
    const f32 half_h = static_cast<f32>(H) * 0.5f;

    const Color bg = opt.background.to_linear();
    const Color glow = opt.glow.with_alpha(std::clamp(opt.intensity * 0.10f, 0.0f, 0.18f)).to_linear();
    const Color line = opt.accent.with_alpha(std::clamp(opt.accent.a * opt.intensity, 0.72f, 0.95f)).to_linear();
    const Color major = Color{line.r, line.g, line.b, 1.0f};

    const f32 spacing = 160.0f;
    const f32 major_step = spacing * 4.0f;
    const f32 glow_thickness = 8.0f;
    const f32 minor_thickness = 2.5f;
    const f32 major_thickness = 5.5f;

    fb.clear(bg);

    for (i32 y = 0; y < H; ++y) {
        const f32 gy = static_cast<f32>(y) - half_h;
        const f32 minor_dy = distance_to_grid_line(gy, spacing);
        const f32 major_dy = distance_to_grid_line(gy, major_step);

        for (i32 x = 0; x < W; ++x) {
            const f32 gx = static_cast<f32>(x) - half_w;
            const f32 minor_dx = distance_to_grid_line(gx, spacing);
            const f32 major_dx = distance_to_grid_line(gx, major_step);

            const bool on_major = (minor_dx <= major_thickness * 0.5f) || (minor_dy <= major_thickness * 0.5f) ||
                                  (major_dx <= major_thickness * 0.5f) || (major_dy <= major_thickness * 0.5f);
            const bool on_minor = (minor_dx <= minor_thickness * 0.5f) || (minor_dy <= minor_thickness * 0.5f);
            const bool on_glow = (minor_dx <= glow_thickness * 0.5f) || (minor_dy <= glow_thickness * 0.5f);

            if (on_glow) {
                Color c = bg;
                c = raster::blend_normal(glow, c);
                fb.set_pixel(x, y, c);
            }
            if (on_minor) {
                Color c = fb.get_pixel(x, y);
                c = raster::blend_normal(line, c);
                fb.set_pixel(x, y, c);
            }
            if (on_major) {
                Color c = fb.get_pixel(x, y);
                c = raster::blend_normal(major, c);
                fb.set_pixel(x, y, c);
            }
        }
    }
}

std::filesystem::path ensure_grid_clean_background_image(i32 width, i32 height, const BackgroundOptions& opt) {
    const auto path = grid_clean_cache_path(width, height);
    if (std::filesystem::exists(path)) {
        return path;
    }

    Framebuffer fb(width, height);
    rasterize_grid_clean_background(fb, opt);
    save_png(fb, path.string());
    return path;
}

void render_grid_clean(SceneBuilder& s, const FrameContext& ctx, const BackgroundOptions& opt) {
    const f32 W = static_cast<f32>(ctx.width);
    const f32 H = static_cast<f32>(ctx.height);
    const f32 shift_x = (opt.animated ? std::max(0.0f, ctx.effective_frame()) : 0.0f) * 0.06f * std::max(opt.speed, 0.0f);

    s.screen_layer("grid_clean_lines", [W, H, opt, shift_x](LayerBuilder& l) {
        l.grid_background("grid", {
            .size = {W, H},
            .offset = {shift_x, 0.0f},
            .bg_color = opt.background,
            .grid_color = Color{1.0f, 1.0f, 1.0f, 0.15f}, // simple elegant white grid
            .spacing = 160.0f,
            .minor_thickness = 1.5f,
            .major_thickness = 3.5f,
            .major_every = 4,
            .centered = true
        });
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
        opt.background = Color{0.006f, 0.008f, 0.014f, 1.0f};
        opt.accent = Color{0.95f, 0.98f, 1.0f, 0.84f};
        opt.glow = Color{0.0f, 0.0f, 0.0f, 0.0f};
        api::render_builtin_background(s, ctx, "grid_clean", opt);
        return s.build();
    });
}

} // namespace

CHRONON_REGISTER_COMPOSITION("GridCleanBackground", grid_clean_background)
