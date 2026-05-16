#pragma once

#include <chronon3d/core/framebuffer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>

namespace chronon3d {

class SoftwareCompositor {
public:
    static void composite_layer(Framebuffer& dst, const Framebuffer& src, BlendMode mode);

private:
    static bool composite_layer_normal_optimized(Framebuffer& dst, const Framebuffer& src);
};

} // namespace chronon3d
