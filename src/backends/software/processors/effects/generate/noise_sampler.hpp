#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <optional>
#include <cstdint>

namespace chronon3d::renderer {

/// Evaluate multi-algorithm noise (Simplex, Worley, Ridged, Value, Blue) at continuous coordinate (x, y).
[[nodiscard]] float evaluate_noise_sample(float x, float y, const NoiseSamplerParams& params,
                                         uint32_t channel_seed, float time_seconds) noexcept;

/// Apply unified NoiseSampler generator to framebuffer.
void apply_noise_sampler(Framebuffer& fb, const NoiseSamplerParams& params,
                         float time_seconds,
                         const std::optional<raster::BBox>& clip = std::nullopt);

} // namespace chronon3d::renderer
