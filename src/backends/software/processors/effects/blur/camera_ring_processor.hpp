#pragma once

// ---------------------------------------------------------------------------
// camera_ring_processor.hpp — software kernel for Petzval field-curvature defocus.
// ---------------------------------------------------------------------------

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/math/raster_utils.hpp>

#include <optional>

namespace chronon3d::renderer {

/// Apply the Camera Ring / Petzval defocus in place. The kernel is a
/// deterministic, energy-normalized Poisson-disc gather: every output pixel's
/// local CoC selects the sampling radius, and the 16 tap weights sum to one.
void apply_camera_ring(Framebuffer& fb, const CameraRingParams& params,
                       const std::optional<raster::BBox>& clip = std::nullopt);

}  // namespace chronon3d::renderer
