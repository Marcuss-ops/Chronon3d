#include <chronon3d/scene/builders/shape_texture_fill.hpp>

#include <stdexcept>

namespace chronon3d {

LayerBuilder& shape_texture_fill(LayerBuilder& layer, const ShapeTextureFillSpec& spec) {
    if (spec.node_name.empty()) {
        throw std::invalid_argument("shape_texture_fill: node_name is required");
    }
    if (spec.shape_layer.empty()) {
        throw std::invalid_argument("shape_texture_fill: shape_layer is required");
    }
    if (!(spec.image.size.x > 0.0f) || !(spec.image.size.y > 0.0f)) {
        throw std::invalid_argument("shape_texture_fill: size must be positive");
    }
    if (!(spec.image.opacity >= 0.0f && spec.image.opacity <= 1.0f)) {
        throw std::invalid_argument("shape_texture_fill: opacity must be in [0, 1]");
    }

    layer.image(spec.node_name, spec.image);
    layer.track_matte_alpha(spec.shape_layer);
    return layer;
}

}  // namespace chronon3d
