#include "render_job.hpp"

#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/core/render_telemetry.hpp>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <filesystem>

namespace chronon3d::cli {

namespace {

bool write_render_frame(const Composition& comp,
                        SoftwareRenderer& renderer,
                        Frame frame,
                        const FrameRange& range,
                        const std::string& output_pattern,
                        bool& ok) {
    const auto layer_count = static_cast<int>(comp.evaluate(frame).layers().size());
    const auto hits_before = renderer.node_cache().stats().hits;
    const auto t0 = std::chrono::steady_clock::now();
    auto fb = renderer.render_frame(comp, frame);
    const auto t1 = std::chrono::steady_clock::now();
    const auto hits_after = renderer.node_cache().stats().hits;

    if (!fb) {
        spdlog::error("Failed to render frame {}", frame);
        ok = false;
        return false;
    }

    const bool is_range = (range.start != range.end);
    const std::string path = format_path(output_pattern, frame, is_range);
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }

    const auto t_png0 = std::chrono::steady_clock::now();
    if (!save_png(*fb, path)) {
        spdlog::error("Failed to save frame {} to {}", frame, path);
        ok = false;
        return false;
    }

    const auto t_png1 = std::chrono::steady_clock::now();
    telemetry::record_render_telemetry({
        .event = "image_render",
        .frame = frame,
        .width = comp.width(),
        .height = comp.height(),
        .total_ms = std::chrono::duration<double, std::milli>(t_png1 - t0).count(),
        .setup_ms = std::chrono::duration<double, std::milli>(t1 - t0).count(),
        .encode_ms = std::chrono::duration<double, std::milli>(t_png1 - t_png0).count(),
        .cache_hit = hits_after > hits_before ? 1 : 0,
        .layer_count = layer_count,
    });
    spdlog::info("Frame {} saved to {}", frame, path);
    return true;
}

} // namespace

std::optional<RenderJobPlan> plan_render_job(const CompositionRegistry& registry,
                                             const RenderArgs& args) {
    auto range = parse_frames(args.frames);
    if (args.frame_old != -1) {
        range.start = range.end = args.frame_old;
    } else if (args.start_old != -1 || args.end_old != -1) {
        if (args.start_old != -1) range.start = args.start_old;
        if (args.end_old != -1) range.end = args.end_old;
    }

    auto resolved = resolve_composition(registry, args.comp_id);
    if (!resolved) {
        return std::nullopt;
    }

    RenderJobPlan plan;
    plan.range = range;
    plan.comp = std::move(resolved.comp);
    plan.comp_id = args.comp_id;
    plan.output = args.output;
    plan.settings = settings_from_args(args, !resolved.from_specscene, args.diagnostic);
    plan.from_specscene = resolved.from_specscene;
    return plan;
}

bool execute_render_job(const CompositionRegistry& registry, const RenderJobPlan& plan) {
    auto renderer = create_renderer(registry, plan.settings);

    if (plan.from_specscene && plan.settings.motion_blur.enabled) {
        spdlog::warn("Motion blur is ignored for specscene inputs in this build");
    }

    spdlog::info("Rendering {} [{} -> {} step {}]{}{}...",
                 plan.comp_id, plan.range.start, plan.range.end, plan.range.step,
                 plan.settings.motion_blur.enabled
                     ? fmt::format(" [MB {}smp {:.0f}°]",
                                   plan.settings.motion_blur.samples,
                                   plan.settings.motion_blur.shutter_angle)
                     : "",
                 plan.settings.ssaa_factor > 1.0f
                     ? fmt::format(" [SSAA {:.1f}x]", plan.settings.ssaa_factor)
                     : "");

    const int64_t effective_end = (plan.range.start == plan.range.end) ? plan.range.start + 1 : plan.range.end;
    bool ok = true;
    for (int64_t f = plan.range.start; f < effective_end; f += plan.range.step) {
        if (!write_render_frame(*plan.comp, *renderer, static_cast<Frame>(f), plan.range, plan.output, ok)) {
            // keep going to report all failures, but preserve false
        }
    }

    spdlog::info("Render complete.");
    return ok;
}

} // namespace chronon3d::cli
