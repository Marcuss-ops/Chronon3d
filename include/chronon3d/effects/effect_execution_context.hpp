#pragma once

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/math/raster_utils.hpp>

#include <optional>

namespace chronon3d::effects {

enum class RenderQuality : uint8_t {
    Preview,
    Final
};

struct EffectExecutionContext {
    float time_seconds{0.0f};
    Frame frame{0};

    std::optional<raster::BBox> clip;

    RenderQuality quality{RenderQuality::Final};
    bool diagnostics_enabled{false};
};

} // namespace chronon3d::effects
