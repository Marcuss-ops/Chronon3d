#include "render_job_plan.hpp"
#include <fmt/format.h>

namespace chronon3d::cli {

RenderJobPlanResult plan_render_job(const RenderArgs& args, bool motion_blur_allowed) {
    RenderJobPlanResult res;
    
    // 1. Validate frames
    auto parsed_frames = parse_frames_safe(args.frames);
    if (!parsed_frames.ok) {
        return {false, {}, fmt::format("Invalid --frames '{}': {}", args.frames, parsed_frames.error)};
    }
    res.value.range = parsed_frames.value;

    // Handle legacy frame args
    if (args.frame_old != -1) {
        res.value.range.start = res.value.range.end = args.frame_old;
    } else if (args.start_old != -1 || args.end_old != -1) {
        if (args.start_old != -1) res.value.range.start = args.start_old;
        if (args.end_old != -1) res.value.range.end = args.end_old;
        
        // Post-legacy validation
        if (res.value.range.end < res.value.range.start) {
            return {false, {}, fmt::format("Legacy range invalid: end ({}) < start ({})", 
                                           res.value.range.end, res.value.range.start)};
        }
    }

    // 2. Validate output
    if (args.output.empty()) {
        return {false, {}, "Output path (--output) is required"};
    }
    res.value.output = args.output;

    // 3. Validate settings
    if (args.ssaa < 1.0f) {
        return {false, {}, fmt::format("SSAA factor must be >= 1.0, got {}", args.ssaa)};
    }
    if (args.motion_blur_samples <= 0) {
        return {false, {}, fmt::format("Motion blur samples must be > 0, got {}", args.motion_blur_samples)};
    }
    if (args.shutter_angle < 0.0f) {
        return {false, {}, fmt::format("Shutter angle must be >= 0, got {}", args.shutter_angle)};
    }

    res.value.comp_id = args.comp_id;
    
    // Fill RenderSettings
    res.value.settings.diagnostic = args.diagnostic;
    res.value.settings.use_modular_graph = args.use_modular_graph;
    res.value.settings.motion_blur.enabled = motion_blur_allowed && args.motion_blur;
    res.value.settings.motion_blur.samples = args.motion_blur_samples;
    res.value.settings.motion_blur.shutter_angle = args.shutter_angle;
    res.value.settings.ssaa_factor = args.ssaa;

    res.ok = true;
    return res;
}

} // namespace chronon3d::cli
