#pragma once

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>

namespace chronon3d::graph::detail {

// Integer translation with per-pixel source clamping (early-return path #1).
void execute_translate_clamped(
    Framebuffer* result, const Framebuffer* input,
    i32 x0, i32 x1, i32 y0, i32 y1,
    f32 x_min_src, f32 x_max_src, f32 y_min_src, f32 y_max_src,
    i32 itx, i32 ity, f32 opacity);

// Integer translation with row-memcpy (early-return path #2).
void execute_translate_memcpy(
    Framebuffer* result, const Framebuffer* input,
    i32 x0, i32 x1, i32 y0, i32 y1,
    i32 itx, i32 ity, f32 opacity);

// Affine (no-perspective) transform — runs a y-range (row_begin … row_end).
void execute_affine_rows(
    Framebuffer* result, const Framebuffer* input,
    i32 x0, i32 x1, i32 y0,
    f32 x_min_src, f32 x_max_src, f32 y_min_src, f32 y_max_src,
    f32 opacity, SamplingMode mode,
    Vec3 h_col_start, Vec3 h_step_y,
    f32 inv_z, f32 dsx, f32 dsy,
    i32 row_begin, i32 row_end);

// Projective (full-perspective) transform — runs a y-range.
void execute_projective_rows(
    Framebuffer* result, const Framebuffer* input,
    i32 x0, i32 x1, i32 y0,
    f32 x_min_src, f32 x_max_src, f32 y_min_src, f32 y_max_src,
    f32 opacity, SamplingMode mode,
    Vec3 h_col_start, Vec3 h_step_x, Vec3 h_step_y,
    i32 row_begin, i32 row_end);

} // namespace chronon3d::graph::detail
