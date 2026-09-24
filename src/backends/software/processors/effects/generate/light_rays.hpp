#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/backends/software/software_session_resources.hpp>
#include <optional>

namespace chronon3d::renderer {

void apply_light_rays(Framebuffer& fb, const LightRaysParams& params,
                      EffectScratchResources& scratch,
                      const std::optional<raster::BBox>& clip = std::nullopt);

} // namespace chronon3d::renderer
