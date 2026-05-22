#pragma once

#include "../blend2d_bridge.hpp"
#include <algorithm>
#include <cmath>

namespace chronon3d::blend2d_bridge {

void sample_bilinear_prgb32(const uint32_t* base, int stride, int sw, int sh, float lx, float ly, float& r, float& g, float& b, float& a);
void sample_bilinear_float(const Color* base, int stride, int sw, int sh, float lx, float ly, Color& result);

} // namespace chronon3d::blend2d_bridge
