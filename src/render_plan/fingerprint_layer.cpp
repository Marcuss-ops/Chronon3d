#include "fingerprint_detail.hpp"

namespace chronon3d::render_plan::fingerprint_detail {

void add_layer(CanonicalPlanWriter& out, const LayerPlan& layer) {
    add_layer_identity_and_mesh(out, layer);
    add_layer_style_and_shape(out, layer);
    add_layer_effects_and_masks(out, layer);
    add_layer_transform_and_timing(out, layer);
}

} // namespace chronon3d::render_plan::fingerprint_detail
