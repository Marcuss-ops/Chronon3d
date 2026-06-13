#pragma once

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <memory>
#include <string>
#include <vector>

namespace chronon3d {
namespace cli {

/// Build a RenderSettings from any args struct that has the standard render fields
/// (pipeline.use_modular_graph, pipeline.quality.motion_blur, etc.).
/// motion_blur_allowed: set to false when the composition type doesn't support it (e.g. specscene).
/// diagnostic: pass args.pipeline.diagnostic if available, otherwise defaults to false.
template<typename Args>
RenderSettings settings_from_args(const Args& args,
                                  bool motion_blur_allowed = true,
                                  bool diagnostic = false) {
    RenderSettings s;
    s.diagnostics.enabled       = diagnostic || args.pipeline.diagnostic;
    s.diagnostics.plan          = args.pipeline.diagnostic_plan;
    s.diagnostics.plan_output   = args.pipeline.diagnostic_plan_output;
    s.use_modular_graph         = args.pipeline.use_modular_graph;
    s.dirty.dirty_rects_v1      = args.pipeline.dirty_rects;
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

/// Result of resolving a composition from a specscene file or registry id.
struct ResolvedComposition {
    std::shared_ptr<Composition> comp;
    bool from_specscene{false};
    std::vector<std::string> diagnostics;

    /// True if the composition was successfully resolved.
    explicit operator bool() const { return comp != nullptr; }
};

/// Resolve a composition from a specscene file path or a registry composition id.
/// Logs errors via spdlog on failure — check the result with operator bool().
ResolvedComposition resolve_composition(const CompositionRegistry& registry,
                                        const std::string& comp_id);

/// Create and configure a SoftwareRenderer from the given settings.
std::shared_ptr<SoftwareRenderer> create_renderer(
    const CompositionRegistry& registry,
    const RenderSettings& settings);

} // namespace cli
} // namespace chronon3d
