#pragma once

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <memory>
#include <optional>
#include <string>

namespace chronon3d {
namespace cli {

/// Concept: Args must have a nested 'pipeline' struct with all the
/// standard render-settings fields that settings_from_args accesses.
/// Intentionally exhaustive — catches missing fields at concept-check
/// time rather than deep in the template body.
template<typename Args>
concept PipelinableArgs = requires(const Args& a) {
    a.pipeline.diagnostic;
    a.pipeline.diagnostic_plan;
    a.pipeline.diagnostic_plan_output;
    a.pipeline.use_modular_graph;
    a.pipeline.no_dirty_rects;
    a.pipeline.tile_size;
    a.pipeline.quality.motion_blur;
    a.pipeline.quality.motion_blur_samples;
    a.pipeline.quality.shutter_angle_deg;
    a.pipeline.quality.shutter_phase_deg;
    a.pipeline.quality.motion_blur_pattern;
    a.pipeline.quality.motion_blur_filter;
    a.pipeline.quality.ssaa;
    a.pipeline.force_scalar_normal_blend;
    a.pipeline.program_cache_capacity;
    a.pipeline.program_cache_tune;
    a.pipeline.program_cache_tune_interval;
    a.pipeline.program_cache_tune_min_capacity;
    a.pipeline.program_cache_tune_max_capacity;
};

/// Build a RenderSettings from any args struct that has the standard render fields
/// (pipeline.use_modular_graph, pipeline.quality.motion_blur, etc.).
/// motion_blur_allowed: set to false for composition types that don't support it.
/// diagnostic: pass args.pipeline.diagnostic if available, otherwise defaults to false.
template<PipelinableArgs Args>
RenderSettings settings_from_args(const Args& args,
                                  bool motion_blur_allowed = true,
                                  bool diagnostic = false) {
    RenderSettings s;
    s.diagnostics.enabled       = diagnostic || args.pipeline.diagnostic;
    s.diagnostics.plan          = args.pipeline.diagnostic_plan;
    s.diagnostics.plan_output   = args.pipeline.diagnostic_plan_output;
    s.use_modular_graph         = args.pipeline.use_modular_graph;
    if (args.pipeline.no_dirty_rects) {
        s.dirty.enabled = false;
        s.dirty.use_bitmask = false;
        s.dirty.use_tiles = false;
    }
    s.dirty.tile_size           = args.pipeline.tile_size;
    s.motion_blur.enabled          = motion_blur_allowed && args.pipeline.quality.motion_blur;
    s.motion_blur.samples          = args.pipeline.quality.motion_blur_samples;
    s.motion_blur.shutter_angle_deg = args.pipeline.quality.shutter_angle_deg;
    s.motion_blur.shutter_phase_deg = args.pipeline.quality.shutter_phase_deg;
    s.motion_blur.pattern          = static_cast<TemporalSamplePattern>(args.pipeline.quality.motion_blur_pattern);
    s.motion_blur.filter           = static_cast<TemporalFilter>(args.pipeline.quality.motion_blur_filter);
    s.ssaa_factor               = args.pipeline.quality.ssaa;
    s.force_scalar_normal_blend = args.pipeline.force_scalar_normal_blend;
    s.program_cache_capacity     = args.pipeline.program_cache_capacity;
    s.program_cache_tune                  = args.pipeline.program_cache_tune;
    s.program_cache_tune_interval         = args.pipeline.program_cache_tune_interval;
    s.program_cache_tune_min_capacity     = args.pipeline.program_cache_tune_min_capacity;
    s.program_cache_tune_max_capacity     = args.pipeline.program_cache_tune_max_capacity;
    return s;
}

/// Result of resolving a composition from the registry.
struct ResolvedComposition {
    std::shared_ptr<Composition> comp;

    /// Legacy flag reflecting whether the composition was loaded via the
    /// (removed) specscene compiler pipeline. Default false. Kept for
    /// ABI compatibility with downstream plan types (VideoJobPlan,
    /// RenderJobPlan) that still mirror this field.
    bool from_specscene{false};

    /// True if the composition was successfully resolved.
    explicit operator bool() const { return comp != nullptr; }
};

/// Resolve a composition from the registry by composition id.
/// Logs errors via spdlog on failure — check the result with operator bool().
ResolvedComposition resolve_composition(const CompositionRegistry& registry,
                                        const std::string& comp_id);

/// Create and configure a SoftwareRenderer from the given settings.
/// If `config` is provided, passes it to SoftwareRenderer's Config-accepting
/// constructor; otherwise uses the default constructor (env-var-based Config).
std::shared_ptr<SoftwareRenderer> create_renderer(
    const CompositionRegistry& registry,
    const RenderSettings& settings,
    const std::optional<Config>& config = std::nullopt);

} // namespace cli
} // namespace chronon3d
