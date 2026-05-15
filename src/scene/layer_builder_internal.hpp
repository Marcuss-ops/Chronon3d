#pragma once

#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/layer/render_node.hpp>
#include <chronon3d/scene/layer/layer.hpp>
#include <memory_resource>
#include <string>

namespace chronon3d::layer_builder_internal {

[[nodiscard]] std::pmr::memory_resource* node_resource(Layer& layer);
[[nodiscard]] RenderNode& append_node(Layer& layer, std::string name);
[[nodiscard]] RenderNode* last_node(Layer& layer);

void append_rect(Layer& layer, std::string name, const RectParams& p);
void append_rounded_rect(Layer& layer, std::string name, const RoundedRectParams& p);
void append_circle(Layer& layer, std::string name, const CircleParams& p);
void append_line(Layer& layer, std::string name, const LineParams& p);
void append_text(Layer& layer, std::string name, TextParams p);
void append_image(Layer& layer, std::string name, ImageParams p);

void set_last_shadow(Layer& layer, DropShadow shadow);
void set_last_glow(Layer& layer, Glow glow);
void set_last_position(Layer& layer, Vec3 pos);
void set_last_rotation(Layer& layer, Vec3 euler_deg);
void set_last_scale(Layer& layer, Vec3 s);
void set_last_anchor(Layer& layer, Vec3 a);
void set_last_opacity(Layer& layer, f32 opacity);

} // namespace chronon3d::layer_builder_internal
