#pragma once

#include <chronon3d/core/framebuffer.hpp>
#include <chronon3d/scene/layer/render_node.hpp>
#include <chronon3d/scene/shape.hpp>

namespace chronon3d {
namespace renderer {

void draw_fake_box3d(Framebuffer& fb, const RenderNode& node, const RenderState& state, const FakeBox3DShape& shape);
void draw_fake_box3d(Framebuffer& fb, const RenderNode& node, const RenderState& state,
                     const FakeBox3DShape& shape, const FakeBox3DRenderState& runtime);

} // namespace renderer
} // namespace chronon3d
