#pragma once

#include <chronon3d/core/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>

#include <algorithm>
#include <random>
#include <string>

namespace chronon3d::presets {

struct ParticleOverlayParams {
    int count{36};
    int streaks{6};
    u32 seed{1337};
    f32 z_offset{0.0f};
    f32 min_radius{1.2f};
    f32 max_radius{4.5f};
    f32 min_alpha{0.12f};
    f32 max_alpha{0.55f};
    Color light_color{Color::white()};
    Color accent_color{Color::from_hex("#ffd1ea")};
    f32 glow_stride{6.0f};
    f32 glow_radius_base{10.0f};
    f32 glow_intensity{0.35f};
    f32 streak_alpha{0.14f};
    f32 streak_thickness{1.5f};
};

inline void particles(SceneBuilder& s,
                      const FrameContext& ctx,
                      std::string name = "particles",
                      ParticleOverlayParams p = {}) {
    std::mt19937 rng(p.seed);
    std::uniform_real_distribution<f32> xdist(0.0f, static_cast<f32>(ctx.width));
    std::uniform_real_distribution<f32> ydist(0.0f, static_cast<f32>(ctx.height));
    std::uniform_real_distribution<f32> radii(p.min_radius, p.max_radius);
    std::uniform_real_distribution<f32> alpha(p.min_alpha, p.max_alpha);
    const int glow_stride = std::max(1, static_cast<int>(p.glow_stride));

    s.layer(std::move(name), [&](LayerBuilder& l) {
        l.enable_3d();

        for (int i = 0; i < p.count; ++i) {
            const bool accent = (i % 3) != 0;
            const Color base = accent ? p.accent_color : p.light_color;
            const Color color = base.with_alpha(alpha(rng));
            l.circle("particle_" + std::to_string(i), {
                .radius = radii(rng),
                .color = color,
                .pos = {xdist(rng), ydist(rng), p.z_offset},
            });
            if (p.glow_stride > 0.0f && (i % glow_stride == 0)) {
                l.with_glow(Glow{
                    .enabled = true,
                    .radius = p.glow_radius_base + static_cast<f32>(i),
                    .intensity = p.glow_intensity,
                    .color = color.with_alpha(color.a * 0.85f),
                });
            }
        }

        for (int i = 0; i < p.streaks; ++i) {
            const f32 x = static_cast<f32>(ctx.width) * (0.12f + 0.14f * static_cast<f32>(i));
            const f32 y = static_cast<f32>(ctx.height) * (0.06f + 0.10f * static_cast<f32>(i % 3));
            l.line("streak_" + std::to_string(i), {
                .from = {x, y, p.z_offset},
                .to = {x + 42.0f, y + 180.0f, p.z_offset},
                .thickness = p.streak_thickness,
                .color = p.light_color.with_alpha(p.streak_alpha),
            });
        }
    });
}

} // namespace chronon3d::presets
