#pragma once

// ---------------------------------------------------------------------------
// diagnostics/layout_preview_overlay.hpp
//
// Per-node visual sanity check:
//   - draws the world-space bbox of the node
//   - marks the anchor with a pink crosshair
//   - marks the screen center with a white crosshair
//   - logs the node's world position, anchor and bbox
// ---------------------------------------------------------------------------

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <chronon3d/scene/model/render/render_runtime.hpp>

namespace chronon3d::renderer {
class ShapeProcessor;
}

namespace chronon3d::renderer::diagnostics {

void draw_layout_preview(Framebuffer& fb, const RenderNode& node, const RenderState& state,
                         renderer::ShapeProcessor& processor);

} // namespace chronon3d::renderer::diagnostics
