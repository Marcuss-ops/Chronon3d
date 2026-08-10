#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/scene/model/render/render_node_params.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>
#include <span>

namespace chronon3d {

class DepthBufferPool;  // forward decl — no include needed for pointer param.

namespace renderer {

void draw_fake_box3d(Framebuffer& fb, const RenderNode& node, const RenderState& state, const FakeBox3DShape& shape);
void draw_fake_box3d(Framebuffer& fb, const RenderNode& node, const RenderState& state,
                     const FakeBox3DShape& shape, const FakeBox3DRenderState& runtime,
                     DepthBufferPool* depth_pool = nullptr);

} // namespace renderer
} // namespace chronon3d
