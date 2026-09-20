// SPDX-License-Identifier: MIT
#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// gpu_text_run_detail.hpp — shared helpers for the gpu_text_run TU family.
//
// `gpu_text_run.cpp` used to hold the whole packed-atlas GPU text path in one
// translation unit.  The path is now split across focused TUs (animation,
// packed-atlas helpers, surface draw, MTSDF, cached bridge); these helpers are
// the only symbols that more than one of them needs, so they live here instead
// of in a TU-local anonymous namespace.
//
// This header is internal-only: it lives under src/ and must never be included
// from include/chronon3d/.
// ═══════════════════════════════════════════════════════════════════════════

#include "gpu_text_run.hpp"

#include <chronon3d/backends/text/text_render_resources.hpp>
#include <chronon3d/runtime/render_surface.hpp>
#include <chronon3d/text/glyph_atlas.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace chronon3d::assets { class AssetResolver; }

namespace chronon3d::graph::text_run {

/// Internal packed-atlas dispatch primitive.  Shared by the public
/// draw_packed_text_run entry point and the cached compatibility bridge; it is
/// not part of the gpu_text_run.hpp contract.
graph::RenderOpResult draw_packed_text_run_surface(
    RenderGraphContext& ctx,
    runtime::RenderSurfaceHandle destination,
    std::span<const GpuTextGlyph> glyphs,
    float current_frame,
    const Color& highlight_color,
    bool highlight_enabled);

/// Internal canonical MTSDF dispatch.  Shared by the cached compatibility
/// bridge only; it is not part of the gpu_text_run.hpp contract.
graph::RenderOpResult draw_cached_text_run_mtsdf(
    RenderGraphContext& ctx,
    Framebuffer& destination,
    const TextRunShape& shape,
    const glm::mat4& model_matrix,
    float opacity);

namespace detail {

/// Overflow-safe `a * b`.  Returns false on overflow and never writes `out`.
/// Glyph dimensions flow in from layout/font/effect code and must never be
/// trusted to fit `size_t` when computing payload sizes.
bool checked_mul(std::size_t a, std::size_t b, std::size_t& out);

struct PackedGlyph {
    std::uint32_t atlas_x{0};
    std::uint32_t atlas_y{0};
};

struct AtlasDims {
    std::uint32_t width{0};
    std::uint32_t height{0};
};

/// Shelf-pack the glyph quads.  Returns the per-glyph atlas origins (in the
/// same order as `glyphs`) plus the resulting atlas dimensions (at least 1x1).
std::pair<std::vector<PackedGlyph>, AtlasDims> pack_glyphs(
    std::span<const GpuTextGlyph> glyphs);

struct TimedHighlightPlan {
    Color color{Color::yellow()};
    std::vector<std::pair<float, float>> glyph_ranges;
    bool enabled{false};
};

TimedHighlightPlan build_timed_highlight_plan(const TextRunShape& shape);

std::optional<GlyphAtlasEntry> rasterize_missing_glyph(
    TextRenderResources& resources,
    const assets::AssetResolver& resolver,
    const std::string& font_path,
    std::uint32_t glyph_id,
    std::uint32_t font_size,
    const Color& color);

} // namespace detail

} // namespace chronon3d::graph::text_run
