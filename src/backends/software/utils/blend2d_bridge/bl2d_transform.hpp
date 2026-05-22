#pragma once

#include "../blend2d_bridge.hpp"
#include "bl2d_sampling.hpp"
#include "bl2d_compositing.hpp"
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#if defined(__AVX2__)
#include <immintrin.h>
#endif

namespace chronon3d::blend2d_bridge {

void composite_bl_image_transformed(Framebuffer& fb, const BLImage& img, const Mat4& model, float opacity, BlendMode mode, const RenderState* state);
void composite_framebuffer_transformed(Framebuffer& dst_fb, const Framebuffer& src_fb, const Mat4& model, float opacity, BlendMode mode, const RenderState* state);

} // namespace chronon3d::blend2d_bridge
