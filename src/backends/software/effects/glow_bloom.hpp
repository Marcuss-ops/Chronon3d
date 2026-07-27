#pragma once

// ---------------------------------------------------------------------------
// glow_bloom.hpp — Bloom-mode runner extracted from glow_pipeline.cpp (FASE 7)
//
// Internal header — NOT part of the public API.  Included only by
// glow_pipeline.cpp.
// ---------------------------------------------------------------------------

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/effects/glow_pipeline.hpp>
#include <optional>

namespace chronon3d::renderer {

/// Run the bloom pipeline pass in-place on `fb`.
/// Extracted from glow_pipeline.cpp::run_bloom_mode() (FASE 7 Step 1).
void run_bloom_mode(Framebuffer& fb,
                    const GlowPipeline& p,
                    const std::optional<raster::BBox>& clip);

} // namespace chronon3d::renderer
