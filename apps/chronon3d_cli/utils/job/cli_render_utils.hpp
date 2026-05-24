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
    s.diagnostic               = diagnostic || args.pipeline.diagnostic;
    s.use_modular_graph        = args.pipeline.use_modular_graph;
    s.dirty_rects              = args.pipeline.dirty_rects;
    s.motion_blur.enabled      = motion_blur_allowed && args.pipeline.quality.motion_blur;
    s.motion_blur.samples      = args.pipeline.quality.motion_blur_samples;
    s.motion_blur.shutter_angle = args.pipeline.quality.shutter_angle;
    s.ssaa_factor              = args.pipeline.quality.ssaa;
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
