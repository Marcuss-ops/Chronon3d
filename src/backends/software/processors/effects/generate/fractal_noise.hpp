#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <optional>
#include <cstdint>

namespace chronon3d::renderer {

/// Shared deterministic smooth value-noise evaluator used by Fractal Noise
/// and spatial distortion effects.
[[nodiscard]] float evaluate_smooth_value_noise(float x, float y, std::uint32_t seed);

void apply_fractal_noise(Framebuffer& fb, const FractalNoiseParams& params,
                         float time_seconds,
                         const std::optional<raster::BBox>& clip = std::nullopt);

} // namespace chronon3d::renderer
