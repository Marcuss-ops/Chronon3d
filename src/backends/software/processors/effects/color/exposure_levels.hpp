#pragma once

// ---------------------------------------------------------------------------
// exposure_levels.hpp — Declarations for scalar Exposure + Levels processors
// ---------------------------------------------------------------------------

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <optional>

namespace chronon3d::renderer {

void apply_exposure(Framebuffer& fb, float stops,
                    const std::optional<raster::BBox>& clip = std::nullopt);

void apply_levels(Framebuffer& fb,
                  float master_in_black, float master_in_white,
                  float master_gamma,
                  float master_out_black, float master_out_white,
                  float red_in_black, float red_in_white,
                  float red_gamma,
                  float red_out_black, float red_out_white,
                  float green_in_black, float green_in_white,
                  float green_gamma,
                  float green_out_black, float green_out_white,
                  float blue_in_black, float blue_in_white,
                  float blue_gamma,
                  float blue_out_black, float blue_out_white,
                  const std::optional<raster::BBox>& clip = std::nullopt);

} // namespace chronon3d::renderer
