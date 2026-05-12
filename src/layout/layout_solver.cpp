#include <chronon3d/layout/layout_solver.hpp>
#include <algorithm>

namespace chronon3d {

Vec2 anchor_position(Anchor anchor, i32 w, i32 h, f32 margin) {
    const f32 W = static_cast<f32>(w);
    const f32 H = static_cast<f32>(h);
    const f32 m = margin;

    switch (anchor) {
        case Anchor::TopLeft:      return {m,         m};
        case Anchor::TopCenter:    return {W * 0.5f,  m};
        case Anchor::TopRight:     return {W - m,     m};
        case Anchor::MiddleLeft:   return {m,         H * 0.5f};
        case Anchor::Center:       return {W * 0.5f,  H * 0.5f};
        case Anchor::MiddleRight:  return {W - m,     H * 0.5f};
        case Anchor::BottomLeft:   return {m,         H - m};
        case Anchor::BottomCenter: return {W * 0.5f,  H - m};
        case Anchor::BottomRight:  return {W - m,     H - m};
        default:                   return {W * 0.5f,  H * 0.5f};
    }
}

void LayoutSolver::solve(Scene& scene, i32 canvas_w, i32 canvas_h) const {
    const f32 W = static_cast<f32>(canvas_w);
    const f32 H = static_cast<f32>(canvas_h);

    for (auto& layer : const_cast<std::pmr::vector<Layer>&>(scene.layers())) {
        if (!layer.layout.enabled) continue;

        // --- Pin ---
        if (layer.layout.pin.has_value()) {
            Vec2 pos = anchor_position(*layer.layout.pin, canvas_w, canvas_h,
                                       layer.layout.margin);
            layer.transform.position.x = pos.x;
            layer.transform.position.y = pos.y;
        }

        // --- Safe area clamp ---
        if (layer.layout.keep_in_safe_area) {
            const SafeArea& sa = layer.layout.safe_area;
            layer.transform.position.x = std::clamp(
                layer.transform.position.x, sa.left, W - sa.right);
            layer.transform.position.y = std::clamp(
                layer.transform.position.y, sa.top, H - sa.bottom);
        }

        // --- fit_text: enable auto_scale on all text nodes in this layer ---
        if (layer.layout.fit_text) {
            for (auto& node : layer.nodes) {
                if (node.shape.type == ShapeType::Text) {
                    node.shape.text.style.auto_scale = true;
                    if (!node.shape.text.box.enabled) {
                        // Enable a default text box matching the canvas
                        node.shape.text.box.enabled  = true;
                        node.shape.text.box.size      = {W, H};
                    }
                }
            }
        }
    }
}

} // namespace chronon3d
