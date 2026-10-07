#pragma once

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/math/raster_utils.hpp>

#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace chronon3d::renderer {
class EffectProcessor;
class ProcessorRegistrySnapshot;
struct EffectProcessorHandle;
}

namespace chronon3d {
class DebugConfig;
class Framebuffer;
class CurveCache;
struct RenderCounters;
struct EffectScratchResources;
}   // TICKET-007: per-instance debug gating

namespace chronon3d::runtime {
struct EffectUsageCounters;
}

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
    // Stable per-layer/per-effect key for temporal processors. Empty on
    // legacy direct dispatch means the effect can render but cannot retain history.
    std::string_view temporal_history_key{};
    const chronon3d::Framebuffer* second_input{nullptr};

    /// Optional job telemetry sink. Set only by graph execution; direct effect
    /// processor users remain independent from JobSession ownership.
    chronon3d::runtime::EffectUsageCounters* effect_usage{nullptr};
    /// True when software executes this stack because a native-capable backend
    /// could not handle its effects. Strict-native requests never take this path.
    bool software_fallback{false};
};

} // namespace chronon3d::effects
