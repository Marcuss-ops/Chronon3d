#pragma once

#include <chronon3d/renderer/software/render_graph.hpp>
#include <chronon3d/scene/shape.hpp>
#include <chronon3d/scene/layer.hpp>
#include <chronon3d/scene/camera.hpp>
#include <chronon3d/scene/camera_2_5d.hpp>
#include <chronon3d/scene/render_node.hpp>

namespace chronon3d {
namespace renderer {

u64 hash_shape(const Shape& shape);
u64 hash_layer(const Layer& layer);
u64 hash_node(const RenderNode& node);
u64 hash_effect_stack(const EffectStack& stack);
u64 hash_camera_2_5d(const Camera2_5D& camera);
u64 hash_camera(const Camera& camera);

} // namespace renderer
} // namespace chronon3d
