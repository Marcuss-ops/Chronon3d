#pragma once

#include <chronon3d/scene/shape.hpp>
#include <chronon3d/renderer/framebuffer.hpp>
#include <chronon3d/math/transform.hpp>

namespace chronon3d {

class ImageRenderer {
public:
    bool draw_image(const ImageShape& image, const RenderState& state, Framebuffer& fb);
};

} // namespace chronon3d
