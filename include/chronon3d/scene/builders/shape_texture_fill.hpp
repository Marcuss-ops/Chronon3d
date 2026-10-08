#pragma once

#include <string>

#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/params/media_params.hpp>

namespace chronon3d {

/// Fills a shape with an image. The shape is a separate layer; its alpha is
/// the track matte of the layer that receives the image. Fit (Cover, Contain,
/// Stretch) and opacity come from `image`.
struct ShapeTextureFillSpec {
    std::string node_name;    // name of the image node inside the layer
    std::string shape_layer;  // layer whose alpha defines the filled shape
    ImageParams image;        // size = shape bounds, fit = Cover/Contain/Stretch
};

/// Adds the image node to `layer` and sets its alpha track matte to
/// `spec.shape_layer`. The track matte applies to the whole layer, so the
/// layer should hold only this image. Throws std::invalid_argument for an
/// empty node or shape name, a non-positive size, or opacity outside [0, 1].
LayerBuilder& shape_texture_fill(LayerBuilder& layer, const ShapeTextureFillSpec& spec);

}  // namespace chronon3d
