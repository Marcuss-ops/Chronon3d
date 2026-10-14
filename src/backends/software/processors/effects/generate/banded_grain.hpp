#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <optional>
#include <cstdint>

namespace chronon3d::renderer {

/// Apply multi-scale banded grain (micro, fine, medium, coarse, macro) to framebuffer with luminance response.
void apply_banded_grain(Framebuffer& fb, const BandedGrainParams& params,
                        float time_seconds,
                        const std::optional<raster::BBox>& clip = std::nullopt);

} // namespace chronon3d::renderer
