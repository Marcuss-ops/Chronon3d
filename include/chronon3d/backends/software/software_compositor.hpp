#pragma once

#include <chronon3d/core/framebuffer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/math/raster_utils.hpp>

namespace chronon3d {

class SoftwareCompositor {
public:
    static void composite_layer(
        Framebuffer& dest,
        const Framebuffer& src,
        BlendMode mode,
        const std::optional<raster::BBox>& clip = std::nullopt
    );

private:
    static bool composite_layer_normal_optimized(Framebuffer& dst, const Framebuffer& src, i32 x0, i32 y0, i32 x1, i32 y1);
};

} // namespace chronon3d
