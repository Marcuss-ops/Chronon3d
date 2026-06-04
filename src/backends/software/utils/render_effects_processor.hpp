#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/scene/model/effect_stack.hpp>
#include <chronon3d/scene/model/layer_effect.hpp>
#include <chronon3d/scene/model/render_node.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <optional>

namespace chronon3d {
namespace renderer {

void apply_blur(Framebuffer& fb, f32 radius, const std::optional<raster::BBox>& clip = std::nullopt, int passes = 3);
void apply_color_effects(Framebuffer& fb, const LayerEffect& effect, const std::optional<raster::BBox>& clip = std::nullopt);
void apply_effect_stack(Framebuffer& fb, const EffectStack& stack, float time_seconds, const std::optional<raster::BBox>& clip = std::nullopt, bool diagnostics_enabled = false);
void apply_fake_3d_wave(Framebuffer& fb, const Fake3DWaveParams& params, float time_seconds);

// ── Adjustment-layer color correction (AE-5) ──
void apply_saturation(Framebuffer& fb, f32 sat, const std::optional<raster::BBox>& clip = std::nullopt);
void apply_hue_rotate(Framebuffer& fb, f32 degrees, const std::optional<raster::BBox>& clip = std::nullopt);
void apply_invert(Framebuffer& fb, f32 amount, const std::optional<raster::BBox>& clip = std::nullopt);
void apply_vignette(Framebuffer& fb, f32 radius, f32 softness, f32 amount, Color color, const std::optional<raster::BBox>& clip = std::nullopt);

void draw_shadow(Framebuffer& fb, const RenderNode& node, const RenderState& state);
void draw_glow(Framebuffer& fb, const RenderNode& node, const RenderState& state);

} // namespace renderer
} // namespace chronon3d
