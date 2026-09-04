#pragma once

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace chronon3d {
namespace cli {

template<typename Args>
concept PipelinableArgs = requires(const Args& a) {
    a.pipeline.diagnostic;
    a.pipeline.diagnostic_plan;
    a.pipeline.diagnostic_plan_output;
    a.pipeline.no_dirty_rects;
    a.pipeline.tile_size;
    a.pipeline.quality.motion_blur;
    a.pipeline.quality.motion_blur_mode;
    a.pipeline.quality.motion_blur_samples;
    a.pipeline.quality.shutter_angle_deg;
    a.pipeline.quality.shutter_phase_deg;
    a.pipeline.quality.motion_blur_pattern;
    a.pipeline.quality.motion_blur_filter;
    a.pipeline.quality.ssaa;
    a.pipeline.force_scalar_normal_blend;
    a.pipeline.text_layout_debug;
    a.pipeline.text_layout_debug_json_path;
    a.pipeline.diagnostic_overlay;
    a.pipeline.diagnostic_overlay_only;
    a.pipeline.program_cache_capacity;
    a.pipeline.program_cache_tune;
    a.pipeline.program_cache_tune_interval;
    a.pipeline.program_cache_tune_min_capacity;
    a.pipeline.program_cache_tune_max_capacity;
};

template<PipelinableArgs Args>
RenderSettings settings_from_args(const Args& args,
                                  bool motion_blur_allowed = true,
                                  bool diagnostic = false) {
    RenderSettings s;
    s.diagnostics.enabled = diagnostic || args.pipeline.diagnostic;
    s.diagnostics.plan = args.pipeline.diagnostic_plan;
    s.diagnostics.plan_output = args.pipeline.diagnostic_plan_output;
    if (args.pipeline.no_dirty_rects) {
        s.dirty.enabled = false;
        s.dirty.use_bitmask = false;
        s.dirty.use_tiles = false;
    }
    s.dirty.tile_size = args.pipeline.tile_size;

    // P2.4: the CLI boundary has already validated and parsed these values.
    // Runtime code only copies canonical typed values; there is no second
    // int/string parser here.
    if (motion_blur_allowed) {
        s.motion_blur.mode = args.pipeline.quality.motion_blur_mode;
        if (s.motion_blur.mode == MotionBlurMode::Off &&
            args.pipeline.quality.motion_blur) {
            s.motion_blur.mode = MotionBlurMode::TemporalAccumulation;
        }
    } else {
        s.motion_blur.mode = MotionBlurMode::Off;
    }
    s.motion_blur.samples = args.pipeline.quality.motion_blur_samples;
    s.motion_blur.shutter_angle_deg = args.pipeline.quality.shutter_angle_deg;
    s.motion_blur.shutter_phase_deg = args.pipeline.quality.shutter_phase_deg;
    s.motion_blur.pattern = args.pipeline.quality.motion_blur_pattern;
    s.motion_blur.filter = args.pipeline.quality.motion_blur_filter;
    s.ssaa_factor = args.pipeline.quality.ssaa;
    s.force_scalar_normal_blend = args.pipeline.force_scalar_normal_blend;
    s.text_layout_debug = args.pipeline.text_layout_debug ||
                          args.pipeline.diagnostic_overlay ||
                          args.pipeline.diagnostic_overlay_only ||
                          !args.pipeline.text_layout_debug_json_path.empty();
    s.text_layout_debug_json_path = args.pipeline.text_layout_debug_json_path;
    s.diagnostic_overlay_only = args.pipeline.diagnostic_overlay_only;
    s.program_cache_capacity = args.pipeline.program_cache_capacity;
    s.program_cache_tune = args.pipeline.program_cache_tune;
    s.program_cache_tune_interval = args.pipeline.program_cache_tune_interval;
    s.program_cache_tune_min_capacity = args.pipeline.program_cache_tune_min_capacity;
    s.program_cache_tune_max_capacity = args.pipeline.program_cache_tune_max_capacity;
    return s;
}

struct ResolvedComposition {
    std::shared_ptr<Composition> comp;
    bool from_specscene{false};
    explicit operator bool() const { return comp != nullptr; }
};

ResolvedComposition resolve_composition(const CompositionRegistry& registry,
                                         const std::string& comp_id);
ResolvedComposition resolve_composition(const CompositionRegistry& registry,
                                         const std::string& comp_id,
                                         const CompositionProps& props);

std::shared_ptr<SoftwareRenderer> create_renderer(
    const CompositionRegistry& registry,
    const RenderSettings& settings,
    std::optional<Config> config = std::nullopt,
    std::optional<std::filesystem::path> assets_root = std::nullopt,
    double* engine_init_ms = nullptr,
    double* backend_init_ms = nullptr);

} // namespace cli
} // namespace chronon3d
