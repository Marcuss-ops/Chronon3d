// tests/backends/software/test_grid_background_kernel_semantics.cpp
//
// CPU-side regression lock for the GPU/CPU semantic parity closeout of
// GridBackgroundShape:
//
//   1. Framebuffer storage is canonical PREMULTIPLIED linear RGBA
//      (Framebuffer::clear contract + Color::premultiplied).  The kernel must
//      clear its translucent background PREMULTIPLIED, otherwise the stored
//      representation differs from the GPU analytic fill and any compositing
//      above a translucent grid produces different pixels on CPU vs GPU.
//   2. The resolved major-line rule (GridBackgroundShape::major_lines_enabled)
//      is shared by every backend: major_every > 1 AND major_thickness > 0.
//      A zero major thickness disables major lines entirely — no feather
//      "ghost" lines at the cell centers.
//   3. The kernel paints exactly the `clip` rectangle passed to it.
//
// The GPU analytic fill (fill_rect.comp kind 5 + VulkanBackend::fill_grid)
// consumes the same resolved numeric fields, so these CPU expectations ARE the
// GPU contract; GPU-side verification runs on a Vulkan host in the parity
// corpus.
#include <doctest/doctest.h>

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/math/transform.hpp>   // RenderState (complete type)

#include "src/backends/software/kernels/grid_background_kernel.hpp"

#include <cmath>

namespace c3d = chronon3d;

namespace {

bool approx_equal(c3d::Color a, c3d::Color b, float eps = 1.0e-6f) {
    return std::abs(a.r - b.r) <= eps && std::abs(a.g - b.g) <= eps &&
           std::abs(a.b - b.b) <= eps && std::abs(a.a - b.a) <= eps;
}

c3d::Color pixel_at(const c3d::Framebuffer& fb, int x, int y) {
    return fb.pixels_row(y)[x];
}

// Pure-background expected value: the kernel clears premultiplied
// bg_linear * (bg.a * opacity).  Mirrors grid_background_kernel colour setup.
c3d::Color expected_background(const c3d::GridBackgroundShape& grid,
                               float opacity) {
    c3d::Color bg = grid.bg_color.to_linear();
    bg.a *= opacity;
    return bg.premultiplied();
}

// Expected premultiplied "over" of a straight-alpha grid line (weight 1.0,
// i.e. a saturated line centre) over the premultiplied background — the exact
// blend_normal formula the kernel applies per pixel.
c3d::Color expected_line_over_background(const c3d::GridBackgroundShape& grid,
                                         float opacity,
                                         float line_alpha_weight,
                                         float major_alpha_scale) {
    const c3d::Color minor = grid.grid_color.to_linear();
    const c3d::Color bg = grid.bg_color.to_linear();

    const float minor_adj_a = minor.a * opacity;
    const float alpha =
        line_alpha_weight * std::min(1.0f, minor_adj_a * major_alpha_scale);
    const c3d::Color premul_bg = expected_background(grid, opacity);

    // blend_normal: out = src.rgb * src.a + dst.rgb * (1 - src.a), dst premult.
    const float out_a = alpha + premul_bg.a * (1.0f - alpha);
    const float out_r = minor.r * alpha + premul_bg.r * (1.0f - alpha);
    const float out_g = minor.g * alpha + premul_bg.g * (1.0f - alpha);
    const float out_b = minor.b * alpha + premul_bg.b * (1.0f - alpha);
    return {out_r, out_g, out_b, out_a};
}

}  // namespace

// ─── 1. Translucent background is stored PREMULTIPLIED ─────────────────────
TEST_CASE(
    "GRID-KERNEL SEMANTICS: translucent background is cleared premultiplied "
    "(CPU/GPU parity contract)") {
    constexpr int kWidth = 128;
    constexpr int kHeight = 64;
    c3d::Framebuffer fb(kWidth, kHeight);

    c3d::GridBackgroundShape grid;
    grid.size = c3d::Vec2{static_cast<c3d::f32>(kWidth),
                          static_cast<c3d::f32>(kHeight)};
    grid.bg_color = c3d::Color{0.30f, 0.40f, 0.50f, 0.5f};   // translucent bg
    grid.grid_color = c3d::Color{0.60f, 0.70f, 0.80f, 0.9f};
    grid.spacing = 32.0f;
    grid.minor_thickness = 8.0f;   // saturated line centres (weight == 1)
    grid.major_thickness = 0.0f;   // major disabled
    grid.major_every = 4;
    grid.centered = false;
    grid.offset = c3d::Vec2{0.0f, 0.0f};

    c3d::RenderState state;
    state.opacity = 0.5f;   // also folds into every alpha

    const c3d::raster::BBox full{0, 0, kWidth, kHeight};
    c3d::renderer::render_grid_background_kernel(fb, grid, full, state);

    // Background-only pixel (far from every grid line: 16px away from the
    // 32px minor cells while line half-width+feather is 4.85px).
    const c3d::Color stored_bg = pixel_at(fb, 16, 16);
    const c3d::Color expected_bg =
        expected_background(grid, state.opacity);
    // RGB must be bg_linear * (bg.a * opacity) — the STRAIGHT value would be
    // ~2x brighter here (bg.a == 0.5) and must never be stored.
    CHECK(approx_equal(stored_bg, expected_bg));
    CHECK(stored_bg.a == doctest::Approx(expected_bg.a));

    // Vertical line centre pixel (0,16): weight 1.0 minor over translucent bg.
    const c3d::Color stored_line = pixel_at(fb, 0, 16);
    const c3d::Color expected_line =
        expected_line_over_background(grid, state.opacity,
                                      /*line_alpha_weight=*/1.0f,
                                      /*major_alpha_scale=*/1.0f);
    CHECK(approx_equal(stored_line, expected_line));
}

// ─── 2. major_thickness == 0 disables major lines (no ghost feathers) ──────
TEST_CASE(
    "GRID-KERNEL SEMANTICS: major_thickness==0 renders identical to "
    "major_every==1 (no ghost major lines)") {
    constexpr int kWidth = 132;
    constexpr int kHeight = 64;
    constexpr int kSpacing = 16;

    const auto render = [&](float major_thickness, int major_every) {
        c3d::Framebuffer fb(kWidth, kHeight);
        c3d::GridBackgroundShape grid;
        grid.size = c3d::Vec2{static_cast<c3d::f32>(kWidth),
                              static_cast<c3d::f32>(kHeight)};
        grid.bg_color = c3d::Color{0.10f, 0.12f, 0.14f, 1.0f};
        grid.grid_color = c3d::Color{0.25f, 0.52f, 1.0f, 0.20f};
        grid.spacing = static_cast<c3d::f32>(kSpacing);
        grid.minor_thickness = 1.25f;
        grid.major_thickness = major_thickness;
        grid.major_every = major_every;
        grid.centered = false;
        grid.offset = c3d::Vec2{0.0f, 0.0f};
        c3d::RenderState state;
        state.opacity = 1.0f;
        const c3d::raster::BBox full{0, 0, kWidth, kHeight};
        c3d::renderer::render_grid_background_kernel(fb, grid, full, state);
        return fb;
    };

    // Major period (4 * 16 = 64px) exists in both, but thickness==0 means the
    // resolved rule disables major lines — output must be pixel-identical to
    // the no-major-period grid.  (Before the closeout the CPU kernel drew
    // 50%-weight feather "ghost" lines at the major cell centres.)
    const auto disabled = render(0.0f, 4);
    const auto no_major = render(2.75f, 1);
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const c3d::Color a = pixel_at(disabled, x, y);
            const c3d::Color b = pixel_at(no_major, x, y);
            INFO("pixel (", x, ", ", y, ")");
            CHECK(approx_equal(a, b, 0.0f));   // bit-identical loops
        }
    }
}

// ─── 3. major_thickness > 0 enables major lines ────────────────────────────
TEST_CASE(
    "GRID-KERNEL SEMANTICS: major lines render when major_thickness > 0") {
    constexpr int kWidth = 132;
    constexpr int kHeight = 64;

    const auto render = [&](float major_thickness, int major_every) {
        c3d::Framebuffer fb(kWidth, kHeight);
        c3d::GridBackgroundShape grid;
        grid.size = c3d::Vec2{static_cast<c3d::f32>(kWidth),
                              static_cast<c3d::f32>(kHeight)};
        grid.bg_color = c3d::Color{0.10f, 0.12f, 0.14f, 1.0f};
        grid.grid_color = c3d::Color{0.25f, 0.52f, 1.0f, 0.20f};
        grid.spacing = 16.0f;
        grid.minor_thickness = 1.25f;
        grid.major_thickness = major_thickness;
        grid.major_every = major_every;
        grid.centered = false;
        grid.offset = c3d::Vec2{0.0f, 0.0f};
        c3d::RenderState state;
        state.opacity = 1.0f;
        const c3d::raster::BBox full{0, 0, kWidth, kHeight};
        c3d::renderer::render_grid_background_kernel(fb, grid, full, state);
        return fb;
    };

    const auto enabled = render(8.0f, 4);   // major step = 64px
    const auto no_major = render(0.0f, 1);

    // The major line centre (64, 8) must be brighter than the same spot with
    // no major period: minor lines at x=64 are saturated (weight 1), so the
    // only difference is the major alpha bump (minor_adj.a * 4 capped at 1).
    // (Background is opaque here, so the "over" alpha saturates at 1.0 in
    // both renders — the discriminator is the premultiplied line RGB.)
    const c3d::Color with_major = pixel_at(enabled, 64, 8);
    const c3d::Color without_major = pixel_at(no_major, 64, 8);
    CHECK(with_major.a == doctest::Approx(without_major.a));   // both saturate
    CHECK(with_major.r > without_major.r);

    // A spot on a minor line but far from any major cell must be identical:
    // x=16 (minor cell), y=8 → major cells are at x ∈ {0,64}; distance 16 >>
    // half-width + feather (4.85).  Major must not bleed into minor lines.
    const c3d::Color minor_only_enabled = pixel_at(enabled, 16, 8);
    const c3d::Color minor_only_no_major = pixel_at(no_major, 16, 8);
    CHECK(approx_equal(minor_only_enabled, minor_only_no_major));
}

// ─── 4a. opacity only (opaque bg, translucent line) ───────────────────────
TEST_CASE(
    "GRID-KERNEL SEMANTICS: opacity<1 with opaque background premultiplies "
    "line + keeps bg storage identical to straight (a==1)") {
    constexpr int kWidth = 64;
    constexpr int kHeight = 32;
    c3d::Framebuffer fb(kWidth, kHeight);

    c3d::GridBackgroundShape grid;
    grid.size = c3d::Vec2{static_cast<c3d::f32>(kWidth),
                          static_cast<c3d::f32>(kHeight)};
    grid.bg_color = c3d::Color{0.20f, 0.22f, 0.24f, 1.0f};   // opaque
    grid.grid_color = c3d::Color{0.50f, 0.80f, 1.0f, 0.9f};
    grid.spacing = 16.0f;
    grid.minor_thickness = 8.0f;   // saturated centres (weight 1)
    grid.major_thickness = 0.0f;
    grid.major_every = 4;
    grid.centered = false;
    grid.offset = c3d::Vec2{0.0f, 0.0f};
    c3d::RenderState state;
    state.opacity = 0.5f;

    const c3d::raster::BBox full{0, 0, kWidth, kHeight};
    c3d::renderer::render_grid_background_kernel(fb, grid, full, state);

    // Opacity folds into the bg alpha too: even an "opaque" bg is stored
    // premultiplied as rgb*(1.0*opacity).
    const c3d::Color bg = expected_background(grid, state.opacity);
    CHECK(approx_equal(pixel_at(fb, 8, 8), bg));

    // Line centre: opacity folds into the line alpha only (bg.a stays 1).
    const c3d::Color line = pixel_at(fb, 0, 8);
    const c3d::Color expected = expected_line_over_background(
        grid, state.opacity, /*line_alpha_weight=*/1.0f,
        /*major_alpha_scale=*/1.0f);
    CHECK(approx_equal(line, expected));
}

// ─── 4b. non-zero offset shifts the grid origin ───────────────────────────
TEST_CASE(
    "GRID-KERNEL SEMANTICS: grid offset shifts the line lattice (non-zero "
    "origin + odd spacing)") {
    constexpr int kWidth = 96;
    constexpr int kHeight = 48;
    c3d::Framebuffer fb(kWidth, kHeight);

    c3d::GridBackgroundShape grid;
    grid.size = c3d::Vec2{static_cast<c3d::f32>(kWidth),
                          static_cast<c3d::f32>(kHeight)};
    grid.bg_color = c3d::Color{0.10f, 0.12f, 0.14f, 1.0f};
    grid.grid_color = c3d::Color{0.60f, 0.80f, 1.0f, 0.9f};
    grid.spacing = 33.0f;             // odd spacing
    grid.minor_thickness = 8.0f;      // saturated centres
    grid.major_thickness = 0.0f;
    grid.major_every = 4;
    grid.centered = false;
    grid.offset = c3d::Vec2{7.0f, 5.0f};   // odd offset
    c3d::RenderState state;
    state.opacity = 1.0f;

    const c3d::raster::BBox full{0, 0, kWidth, kHeight};
    c3d::renderer::render_grid_background_kernel(fb, grid, full, state);

    // Vertical lattice at x = 7 + 33k → saturated at x == 7.
    // Horizontal lattice at y = 5 + 33k; y = 20 is off every horizontal line.
    const c3d::Color centre = pixel_at(fb, 7, 20);
    const c3d::Color expected = expected_line_over_background(
        grid, state.opacity, /*line_alpha_weight=*/1.0f,
        /*major_alpha_scale=*/1.0f);
    CHECK(approx_equal(centre, expected));

    // Far from any lattice line (x = 7 + 16): pure background.
    CHECK(approx_equal(pixel_at(fb, 23, 20),
                       expected_background(grid, state.opacity)));
}

// ─── 4. The kernel honours the clip rectangle ──────────────────────────────
TEST_CASE("GRID-KERNEL SEMANTICS: paint is confined to the clip rectangle") {
    constexpr int kWidth = 128;
    constexpr int kHeight = 64;
    c3d::Framebuffer fb(kWidth, kHeight);

    // Sentinel: opaque white everywhere, so any untouched pixel is detectable.
    const c3d::Color sentinel{1.0f, 1.0f, 1.0f, 1.0f};
    for (int y = 0; y < kHeight; ++y) {
        c3d::Color* row = fb.pixels_row(y);
        for (int x = 0; x < kWidth; ++x) row[x] = sentinel;
    }

    c3d::GridBackgroundShape grid;
    grid.size = c3d::Vec2{static_cast<c3d::f32>(kWidth),
                          static_cast<c3d::f32>(kHeight)};
    grid.bg_color = c3d::Color{0.05f, 0.06f, 0.07f, 1.0f};
    grid.grid_color = c3d::Color{0.20f, 0.50f, 1.0f, 0.10f};
    grid.spacing = 16.0f;
    grid.minor_thickness = 1.25f;
    grid.major_thickness = 0.0f;
    grid.major_every = 4;
    grid.centered = false;
    grid.offset = c3d::Vec2{0.0f, 0.0f};
    c3d::RenderState state;
    state.opacity = 1.0f;

    const c3d::raster::BBox clip{40, 20, 80, 44};
    c3d::renderer::render_grid_background_kernel(fb, grid, clip, state);

    const c3d::Color expected_bg = expected_background(grid, state.opacity);
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const c3d::Color c = pixel_at(fb, x, y);
            if (x >= clip.x0 && x < clip.x1 && y >= clip.y0 && y < clip.y1) {
                // Inside the clip everything is painted (bg or a grid line).
                if (x == 60 && y == 30) {
                    // Background-only spot inside the clip: 60 is 4px off a
                    // minor cell (64) and 20px off a major cell — thin minor
                    // (1.25px) feathers to zero at 4px.
                    CHECK(approx_equal(c, expected_bg));
                }
            } else {
                // Outside the clip: untouched sentinel.
                CHECK(approx_equal(c, sentinel));
            }
        }
    }
}
