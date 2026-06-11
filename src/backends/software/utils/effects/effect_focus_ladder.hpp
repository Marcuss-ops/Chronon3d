#pragma once

// ---------------------------------------------------------------------------
// effect_focus_ladder.hpp — Declarations for the precomputed blur ladder
//
// Extracted from effect_stack.cpp to keep the dispatcher focused on
// orchestration rather than implementation details.
// ---------------------------------------------------------------------------

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <optional>

namespace chronon3d::renderer {

/// Apply precomputed blur ladder with crossfade between levels.
/// Pre-renders N blur levels on first frame (or size change) and caches them
/// in a static LRU-like map, then crossfades between adjacent levels.
void apply_focus_in_ladder(Framebuffer& fb, const FocusInLadderParams& p,
                           float time_seconds,
                           const std::optional<raster::BBox>& clip);

} // namespace chronon3d::renderer
