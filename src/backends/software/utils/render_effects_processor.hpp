#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/scene/effects/effect_stack.hpp>
#include <chronon3d/scene/effects/layer_effect.hpp>
#include <chronon3d/scene/layer/render_node.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <optional>

namespace chronon3d {
namespace renderer {

void apply_blur(Framebuffer& fb, f32 radius, const std::optional<raster::BBox>& clip = std::nullopt);
void apply_color_effects(Framebuffer& fb, const LayerEffect& effect, const std::optional<raster::BBox>& clip = std::nullopt);
void apply_effect_stack(Framebuffer& fb, const EffectStack& stack, float time_seconds, const std::optional<raster::BBox>& clip = std::nullopt);
void apply_fake_3d_wave(Framebuffer& fb, const Fake3DWaveParams& params, float time_seconds);

void draw_shadow(Framebuffer& fb, const RenderNode& node, const RenderState& state);
void draw_glow(Framebuffer& fb, const RenderNode& node, const RenderState& state);

} // namespace renderer
} // namespace chronon3d
