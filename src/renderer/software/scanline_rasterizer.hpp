#pragma once

#include <chronon3d/core/framebuffer.hpp>
#include <chronon3d/math/vec2.hpp>
#include <chronon3d/math/color.hpp>

namespace chronon3d {
namespace renderer {

void fill_convex_quad(Framebuffer& fb, const Vec2 v[4], const Color& color);
void fill_gradient_quad(Framebuffer& fb, const Vec2 v[4], const Color colors[4]);

void fill_triangle(Framebuffer& fb, const Vec2 v[3], const Color& color);
void fill_gradient_triangle(Framebuffer& fb, const Vec2 v[3], const Color colors[3]);

} // namespace renderer
} // namespace chronon3d
