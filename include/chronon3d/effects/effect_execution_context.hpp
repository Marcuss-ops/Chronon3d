#pragma once

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/math/raster_utils.hpp>

#include <optional>

namespace chronon3d { class DebugConfig; }   // TICKET-007: per-instance debug gating

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

    /// TICKET-007: per-instance DebugConfig forwarded from
    /// RenderGraphContext::options::debug_config.  Replaces the
    /// removed process-wide `detail::g_debug_config`.  When
    /// nullptr, debug overlays / per-pass artifacts are skipped.
    const chronon3d::DebugConfig* debug_cfg{nullptr};
};

} // namespace chronon3d::effects
