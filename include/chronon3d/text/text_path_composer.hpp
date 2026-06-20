#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// text_path_composer.hpp — Text-on-path placement engine
//
// PR 5 of the text pipeline.  Uses PathSampler to position HarfBuzz-shaped
// clusters along a Bézier path, respecting alignment, margins, overflow,
// and per-cluster rotation.
//
// Usage:
//   auto sampler = PathSampler::compile(*spec.path);
//   auto placed  = compose_text_on_path(shaped, sampler, spec, source_text);
//   // placed.clusters[i].position = {x, y} — world-space position
//   // placed.clusters[i].rotation_deg = 45.0f — tangent angle
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/path_sampler.hpp>
#include <chronon3d/text/text_path_spec.hpp>

#include <string_view>
#include <vector>

namespace chronon3d {

// ── PlacedCluster — a cluster positioned on a path ─────────────────────

struct PlacedCluster {
    /// Index into PlacedGlyphRun::clusters.
    size_t cluster_index{0};

    /// World-space position of the cluster centre on the path.
    Vec2 position{0.0f, 0.0f};

    /// Rotation in degrees (0 = upright, + = clockwise).
    /// Only meaningful when perpendicular_to_path is true.
    float rotation_deg{0.0f};

    /// Distance along the path where this cluster was placed.
    float path_distance{0.0f};
};

// ── PlacedTextOnPath — result of text-on-path composition ──────────────

struct PlacedTextOnPath {
    /// One entry per cluster (same order as PlacedGlyphRun::clusters).
    /// Clusters that overflow and are clipped have position = {0,0}.
    std::vector<PlacedCluster> clusters;
};

/// Compose text along a path using an already-compiled PathSampler.
///
/// @param shaped       Already-shaped glyph run with populated clusters.
/// @param sampler      Pre-compiled PathSampler for the target path.
/// @param spec         Text-on-path style (margins, alignment, overflow, etc.).
/// @param source_text  Original UTF-8 text (for RTL detection — not yet used).
/// @return PlacedTextOnPath with one PlacedCluster per input cluster.
[[nodiscard]] PlacedTextOnPath compose_text_on_path(
    const PlacedGlyphRun& shaped,
    const PathSampler& sampler,
    const TextPathSpec& spec,
    std::string_view source_text = {}
);

} // namespace chronon3d
