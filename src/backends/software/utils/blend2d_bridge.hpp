#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <blend2d.h>

#include <chronon3d/scene/model/render_node.hpp>

namespace chronon3d::blend2d_bridge {

bool pixel_passes_mask(const RenderState& state, i32 x, i32 y);

void sample_bilinear_prgb32(const uint32_t* base, int stride, int sw, int sh, float lx, float ly, float& r, float& g, float& b, float& a);

void sample_bilinear_float(const Color* base, int stride, int sw, int sh, float lx, float ly, Color& result);

void composite_bl_image(Framebuffer& fb, const BLImage& img, int x, int y, float opacity, BlendMode mode, const RenderState* state = nullptr);

void composite_framebuffer(Framebuffer& dst_fb, const Framebuffer& src_fb, int x, int y, float opacity, BlendMode mode, const RenderState* state = nullptr);

void composite_bl_image_transformed(Framebuffer& fb, const BLImage& img, const Mat4& model, float opacity, BlendMode mode, const RenderState* state = nullptr, float radius = 0.0f);

void composite_bl_image_tiled(Framebuffer& fb, const BLImage& img, const Mat4& model, float opacity, BlendMode mode, const RenderState* state = nullptr);

void composite_framebuffer_transformed(Framebuffer& dst_fb, const Framebuffer& src_fb, const Mat4& model, float opacity, BlendMode mode, const RenderState* state = nullptr);

} // namespace chronon3d::blend2d_bridge
