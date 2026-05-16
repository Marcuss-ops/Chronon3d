#include "../commands.hpp"
#include "../utils/cli_utils.hpp"
#include "../utils/cli_render_utils.hpp"
#include "../utils/render_job_plan.hpp"
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/core/render_telemetry.hpp>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fmt/format.h>
#include <chrono>

namespace chronon3d {
namespace cli {

int command_render(const CompositionRegistry& registry, const RenderArgs& args) {
    auto resolved = resolve_composition(registry, args.comp_id);
    if (!resolved) return 1;

    auto plan_res = plan_render_job(args, !resolved.from_specscene);
    if (!plan_res.ok) {
        spdlog::error("Render plan failed: {}", plan_res.error);
        return 1;
    }
    const auto& plan = plan_res.value;
    const auto& range = plan.range;

    auto renderer = create_renderer(registry, plan.settings);

    if (resolved.from_specscene && args.motion_blur) {
        spdlog::warn("Motion blur is ignored for specscene inputs in this build");
    }

    spdlog::info("Rendering {} [{} -> {} step {}]{}{}...",
        plan.comp_id, range.start, range.end, range.step,
        plan.settings.motion_blur.enabled ? fmt::format(" [MB {}smp {:.0f}°]", plan.settings.motion_blur.samples, plan.settings.motion_blur.shutter_angle) : "",
        plan.settings.ssaa_factor > 1.0f ? fmt::format(" [SSAA {:.1f}x]", plan.settings.ssaa_factor) : "");

    int64_t effective_end = (range.start == range.end) ? range.start + 1 : range.end;
    bool ok = true;
    for (int64_t f = range.start; f < effective_end; f += range.step) {
        const auto layer_count = static_cast<int>(resolved.comp->evaluate(static_cast<Frame>(f)).layers().size());
        const auto hits_before = renderer->node_cache().stats().hits;
        const auto t0 = std::chrono::steady_clock::now();
        auto fb = renderer->render_frame(*resolved.comp, static_cast<Frame>(f));
        const auto t1 = std::chrono::steady_clock::now();
        const auto hits_after = renderer->node_cache().stats().hits;

        if (fb) {
            bool is_range = (range.start != range.end);
            std::string path = format_path(plan.output, f, is_range);
            std::filesystem::path p(path);
            if (p.has_parent_path()) {
                std::filesystem::create_directories(p.parent_path());
            }

            const auto t_png0 = std::chrono::steady_clock::now();
            if (save_png(*fb, path)) {
                const auto t_png1 = std::chrono::steady_clock::now();
                telemetry::record_render_telemetry({
                    .event = "image_render",
                    .frame = static_cast<Frame>(f),
                    .width = resolved.comp->width(),
                    .height = resolved.comp->height(),
                    .total_ms = std::chrono::duration<double, std::milli>(t_png1 - t0).count(),
                    .setup_ms = std::chrono::duration<double, std::milli>(t1 - t0).count(),
                    .encode_ms = std::chrono::duration<double, std::milli>(t_png1 - t_png0).count(),
                    .cache_hit = hits_after > hits_before ? 1 : 0,
                    .layer_count = layer_count,
                });
                spdlog::info("Frame {} saved to {}", f, path);
            } else {
                spdlog::error("Failed to save frame {} to {}", f, path);
                ok = false;
            }
        } else {
            spdlog::error("Failed to render frame {}", f);
            ok = false;
        }
    }

    spdlog::info("Render complete.");
    return ok ? 0 : 1;
}

} // namespace cli
} // namespace chronon3d
