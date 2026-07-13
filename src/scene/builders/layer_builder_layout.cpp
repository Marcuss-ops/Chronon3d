// ============================================================================
// layer_builder_layout.cpp — LayerBuilder layout-domain methods
// ============================================================================
//
// Contains layout-related LayerBuilder methods: pin_to, keep_in_safe_area,
// and fit_text. Extracted from layer_builder_core.cpp as part of the
// domain split (core, transform, layout, text, shapes, effects, media, masks).

#include <chronon3d/scene/builders/layer_builder.hpp>

namespace chronon3d {

LayerBuilder& LayerBuilder::pin_to(Anchor anchor, f32 margin) {
    return pin_to(AnchorPlacement{anchor}, margin);
}

LayerBuilder& LayerBuilder::pin_to(AnchorPlacement placement, f32 margin) {
    m_layer.layout.enabled = true;
    m_layer.layout.pin     = placement;
    m_layer.layout.margin  = margin;
    return *this;
}

LayerBuilder& LayerBuilder::keep_in_safe_area(SafeArea area) {
    m_layer.layout.enabled           = true;
    m_layer.layout.keep_in_safe_area = true;
    m_layer.layout.safe_area         = area;
    return *this;
}

LayerBuilder& LayerBuilder::fit_text() {
    m_layer.layout.enabled  = true;
    m_layer.layout.fit_text = true;
    return *this;
}

} // namespace chronon3d
