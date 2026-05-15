#include "../commands.hpp"
#include "../utils/cli_utils.hpp"
#include "../utils/cli_render_utils.hpp"
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/core/render_telemetry.hpp>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fmt/format.h>
#include <chrono>

namespace chronon3d {
namespace cli {

int command_render(const CompositionRegistry& registry, const RenderArgs& args) {
    auto range = parse_frames(args.frames);
    bool ok = true;

    if (args.frame_old != -1) {
        range.start = range.end = args.frame_old;
    } else if (args.start_old != -1 || args.end_old != -1) {
        if (args.start_old != -1) range.start = args.start_old;
        if (args.end_old != -1) range.end = args.end_old;
    }

    auto resolved = resolve_composition(registry, args.comp_id);
    if (!resolved) return 1;

    RenderSettings settings;
    settings.diagnostic         = args.diagnostic;
    settings.use_modular_graph  = args.use_modular_graph;
    settings.motion_blur.enabled      = resolved.from_specscene ? false : args.motion_blur;
    settings.motion_blur.samples      = args.motion_blur_samples;
    settings.motion_blur.shutter_angle = args.shutter_angle;
    settings.ssaa_factor              = args.ssaa;

    auto renderer = create_renderer(registry, settings);

    if (resolved.from_specscene && args.motion_blur) {
        spdlog::warn("Motion blur is ignored for specscene inputs in this build");
    }

    spdlog::info("Rendering {} [{} -> {} step {}]{}{}...",
        args.comp_id, range.start, range.end, range.step,
        args.motion_blur ? fmt::format(" [MB {}smp {:.0f}°]", args.motion_blur_samples, args.shutter_angle) : "",
        args.ssaa > 1.0f ? fmt::format(" [SSAA {:.1f}x]", args.ssaa) : "");

    int64_t effective_end = (range.start == range.end) ? range.start + 1 : range.end;
    for (int64_t f = range.start; f < effective_end; f += range.step) {
        const auto layer_count = static_cast<int>(resolved.comp->evaluate(static_cast<Frame>(f)).layers().size());
        const auto hits_before = renderer->node_cache().stats().hits;
        const auto t0 = std::chrono::steady_clock::now();
        auto fb = renderer->render_frame(*resolved.comp, static_cast<Frame>(f));
        const auto t1 = std::chrono::steady_clock::now();
        const auto hits_after = renderer->node_cache().stats().hits;

        if (fb) {
            bool is_range = (range.start != range.end);
            std::string path = format_path(args.output, f, is_range);
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
