#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/color.hpp>
#include <span>

namespace chronon3d {
namespace renderer {

// ── Flat (2D) scanline rasterizers — no depth testing ──────────────────────

void fill_convex_quad(Framebuffer& fb, const Vec2 v[4], const Color& color);
void fill_gradient_quad(Framebuffer& fb, const Vec2 v[4], const Color colors[4]);

void fill_triangle(Framebuffer& fb, const Vec2 v[3], const Color& color);
void fill_gradient_triangle(Framebuffer& fb, const Vec2 v[3], const Color colors[3]);

// ── 3D scanline rasterizers — with per-pixel depth test ────────────────────
//
// Vec3 corners carry z = camera-space depth (positive Z forward).
// When depth_buffer is non-empty (w×h), performs per-pixel depth comparison
// and writes interpolated Z to the depth buffer.

void fill_convex_quad(Framebuffer& fb, const Vec3 v[4], const Color& color,
                       std::span<float> depth_buffer);

void fill_gradient_quad(Framebuffer& fb, const Vec3 v[4], const Color colors[4],
                         std::span<float> depth_buffer);

void fill_triangle(Framebuffer& fb, const Vec3 v[3], const Color& color,
                    std::span<float> depth_buffer);

void fill_gradient_triangle(Framebuffer& fb, const Vec3 v[3], const Color colors[3],
                             std::span<float> depth_buffer);

} // namespace renderer
} // namespace chronon3d
