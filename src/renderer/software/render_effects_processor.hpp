#pragma once

#include <chronon3d/renderer/software/framebuffer.hpp>
#include <chronon3d/scene/effect_stack.hpp>
#include <chronon3d/scene/layer_effect.hpp>
#include <chronon3d/scene/render_node.hpp>
#include <chronon3d/math/transform.hpp>

namespace chronon3d {
namespace renderer {

void apply_blur(Framebuffer& fb, f32 radius);
void apply_effect_stack(Framebuffer& fb, const EffectStack& stack);
void apply_color_effects(Framebuffer& fb, const LayerEffect& effect);

void draw_shadow(Framebuffer& fb, const RenderNode& node, const RenderState& state);
void draw_glow(Framebuffer& fb, const RenderNode& node, const RenderState& state);

} // namespace renderer
} // namespace chronon3d
