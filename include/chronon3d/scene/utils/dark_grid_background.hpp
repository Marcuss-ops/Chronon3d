#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/core/framebuffer.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>

namespace chronon3d::scene::utils {

namespace detail {

inline u64 mix_hash(u64 seed, u64 value) {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    return seed;
}

inline u64 hash_float(f32 value) {
    return static_cast<u64>(std::bit_cast<std::uint32_t>(value));
}

inline u64 hash_color(const Color& c) {
    u64 seed = 0;
    seed = mix_hash(seed, hash_float(c.r));
    seed = mix_hash(seed, hash_float(c.g));
    seed = mix_hash(seed, hash_float(c.b));
    seed = mix_hash(seed, hash_float(c.a));
    return seed;
}

inline std::filesystem::path cache_dir() {
    return std::filesystem::path("output") / "cache" / "dark_grid_background";
}

inline std::filesystem::path cache_path(f32 W, f32 H, const DarkGridBgParams& p) {
    u64 seed = 0;
    seed = mix_hash(seed, hash_float(W));
    seed = mix_hash(seed, hash_float(H));
    seed = mix_hash(seed, hash_color(p.bg_color));
    seed = mix_hash(seed, hash_color(p.grid_color));
    seed = mix_hash(seed, hash_float(p.spacing));
    seed = mix_hash(seed, hash_float(p.extent));
    seed = mix_hash(seed, static_cast<u64>(p.centered));

    std::ostringstream oss;
    oss << std::hex << seed << ".png";
    return cache_dir() / oss.str();
}

inline f32 line_weight(f32 distance, f32 thickness) {
    const f32 half = thickness * 0.5f;
    const f32 feather = 0.85f;
    const f32 edge0 = half + feather;
    const f32 edge1 = half - feather;
    if (distance <= edge1) return 1.0f;
    if (distance >= edge0) return 0.0f;
    return 1.0f - (distance - edge1) / std::max(edge0 - edge1, 1e-6f);
}

inline void rasterize_dark_grid_background(Framebuffer& fb, const DarkGridBgParams& p) {
    const i32 W = fb.width();
    const i32 H = fb.height();
    const f32 half_w = static_cast<f32>(W) * 0.5f;
    const f32 half_h = static_cast<f32>(H) * 0.5f;
    const Color bg = p.bg_color.to_linear();
    const Color minor = p.grid_color.to_linear();
    Color major = minor;
    major.a = std::min(1.0f, major.a * 4.0f);

    const f32 minor_step = std::max(p.spacing, 1.0f);
    const f32 major_step = minor_step * 4.0f;

    for (i32 y = 0; y < H; ++y) {
        const f32 gy = p.centered ? (static_cast<f32>(y) - half_h) : static_cast<f32>(y);
        const f32 minor_dy = std::abs(gy - std::round(gy / minor_step) * minor_step);
        const f32 major_dy = std::abs(gy - std::round(gy / major_step) * major_step);
        for (i32 x = 0; x < W; ++x) {
            const f32 gx = p.centered ? (static_cast<f32>(x) - half_w) : static_cast<f32>(x);
            const f32 minor_dx = std::abs(gx - std::round(gx / minor_step) * minor_step);
            const f32 major_dx = std::abs(gx - std::round(gx / major_step) * major_step);

            const f32 minor_alpha = std::max(line_weight(minor_dx, 1.25f), line_weight(minor_dy, 1.25f)) * minor.a;
            const f32 major_alpha = std::max(line_weight(major_dx, 2.75f), line_weight(major_dy, 2.75f)) * major.a;

            Color c = bg;
            if (minor_alpha > 0.0f) c = lerp(c, minor, minor_alpha);
            if (major_alpha > 0.0f) c = lerp(c, major, major_alpha);
            fb.set_pixel(x, y, c);
        }
    }
}

inline std::filesystem::path ensure_dark_grid_background_image(
    i32 width,
    i32 height,
    const DarkGridBgParams& p
) {
    const auto path = cache_path(static_cast<f32>(width), static_cast<f32>(height), p);
    if (std::filesystem::exists(path)) {
        return path;
    }

    std::filesystem::create_directories(path.parent_path());

    Framebuffer fb(width, height);
    rasterize_dark_grid_background(fb, p);
    save_png(fb, path.string());
    return path;
}

} // namespace detail

inline void dark_grid_background(SceneBuilder& s,
                                const FrameContext& ctx,
                                const DarkGridBgParams& p = {}) {
    const f32 W = static_cast<f32>(ctx.width > 0 ? ctx.width : 1280);
    const f32 H = static_cast<f32>(ctx.height > 0 ? ctx.height : 720);
    const auto grid_path = detail::ensure_dark_grid_background_image(static_cast<i32>(W), static_cast<i32>(H), p);

    s.layer("nbg_bg", [grid_path, W, H](LayerBuilder& l) {
        l.cache_static();
        l.image("grid_bg", {
            .path = grid_path.string(),
            .size = {W, H},
            // Images are centered on their anchor, so place the grid at screen center.
            .pos = {W * 0.5f, H * 0.5f, 0.0f},
            .opacity = 1.0f
        });
    });
}

inline Composition dark_grid_background_scene(
    i32 width = 1280,
    i32 height = 720,
    const DarkGridBgParams& p = {},
    Frame duration = 1
) {
    CompositionSpec spec{
        .name = "DarkGridBackground",
        .width = width,
        .height = height,
        .duration = duration
    };

    return Composition(spec, [p](const FrameContext& ctx) {
        SceneBuilder s(ctx.resource);
        dark_grid_background(s, ctx, p);
        return s.build();
    });
}

} // namespace chronon3d::scene::utils
