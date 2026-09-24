#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <optional>

namespace chronon3d::renderer {

void apply_fractal_noise(Framebuffer& fb, const FractalNoiseParams& params,
                         float time_seconds,
                         const std::optional<raster::BBox>& clip = std::nullopt);

} // namespace chronon3d::renderer
