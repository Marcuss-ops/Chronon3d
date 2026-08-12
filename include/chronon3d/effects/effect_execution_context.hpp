#pragma once

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/math/raster_utils.hpp>

#include <memory>
#include <optional>
#include <span>

namespace chronon3d::renderer {
class EffectProcessor;
class ProcessorRegistrySnapshot;
struct EffectProcessorHandle;
}

namespace chronon3d {
class DebugConfig;
class CurveCache;
struct RenderCounters;
struct EffectScratchResources;
}   // TICKET-007: per-instance debug gating

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
    chronon3d::CurveCache* curve_cache{nullptr};
    chronon3d::RenderCounters* counters{nullptr};

    // Effect processors are resolved while compiling the render graph.
    // The span is aligned with the authored EffectStack (disabled entries
    // retain an invalid handle), so execution never consults the mutable
    // registry. The owning snapshot is retained by the node-local context.
    std::span<const chronon3d::renderer::EffectProcessorHandle> effect_processors{};
    chronon3d::EffectScratchResources* effect_scratch{nullptr};
    std::shared_ptr<const chronon3d::renderer::ProcessorRegistrySnapshot>
        processor_snapshot;
    bool processors_resolved{false};
};

} // namespace chronon3d::effects
