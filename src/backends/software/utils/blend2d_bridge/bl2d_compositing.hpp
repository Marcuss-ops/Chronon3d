#pragma once

#include "../blend2d_bridge.hpp"
#include "bl2d_sampling.hpp"
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

namespace chronon3d::blend2d_bridge {

void composite_bl_image(Framebuffer& fb, const BLImage& img, int x, int y, float opacity, BlendMode mode, const RenderState* state);
void composite_framebuffer(Framebuffer& dst_fb, const Framebuffer& src_fb, int x, int y, float opacity, BlendMode mode, const RenderState* state);

} // namespace chronon3d::blend2d_bridge
