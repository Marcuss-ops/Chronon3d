// SPDX-License-Identifier: MIT
#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// gpu_text_run_detail.hpp — shared helpers for the gpu_text_run TU family.
//
// The text path is split across focused TUs (animation, MTSDF and cached
// bridge); these helpers are the only symbols that more than one of them
// needs, so they live here instead of in a TU-local anonymous namespace.
//
// This header is internal-only: it lives under src/ and must never be included
// from include/chronon3d/.
// ═══════════════════════════════════════════════════════════════════════════

#include "gpu_text_run.hpp"

#include <chronon3d/backends/text/text_resource_contracts.hpp>
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

/// Internal canonical MTSDF dispatch. Shared by the cached bridge only; it is
/// not part of the gpu_text_run.hpp contract.
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

} // namespace detail

} // namespace chronon3d::graph::text_run
