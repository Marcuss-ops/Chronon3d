#include "../commands.hpp"
#include <chronon3d/renderer/software/software_renderer.hpp>
#include <spdlog/spdlog.h>
#include <fmt/format.h>
#include <chrono>

namespace chronon3d {
namespace cli {

int command_bench(const CompositionRegistry& registry, const BenchArgs& args) {
    if (!registry.contains(args.comp_id)) {
        spdlog::error("Unknown composition: {}", args.comp_id);
        return 1;
    }
    if (args.frames <= 0) {
        spdlog::error("--frames must be greater than zero");
        return 1;
    }
    if (args.warmup < 0) {
        spdlog::error("--warmup must be zero or greater");
        return 1;
    }

    auto comp = registry.create(args.comp_id);
    SoftwareRenderer renderer;
    renderer.set_composition_registry(&registry);
    RenderSettings settings;
    settings.use_modular_graph = args.use_modular_graph;
    renderer.set_settings(settings);

    spdlog::info("Benchmarking {} (warmup: {}, frames: {})", args.comp_id, args.warmup, args.frames);

    for (int i = 0; i < args.warmup; ++i) {
        const auto frame = static_cast<Frame>(i);
        auto scene = comp.evaluate(frame);
        auto fb = renderer.render_scene(scene, comp.camera, comp.width(), comp.height());
        if (!fb) {
            spdlog::error("Warmup render failed at frame {}", frame);
            return 1;
        }
    }

    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < args.frames; ++i) {
        const auto frame = static_cast<Frame>(args.warmup + i);
        auto scene = comp.evaluate(frame);
        auto fb = renderer.render_scene(scene, comp.camera, comp.width(), comp.height());
        if (!fb) {
            spdlog::error("Render failed at frame {}", frame);
            return 1;
        }
    }
    const auto t1 = std::chrono::steady_clock::now();

    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(t1 - t0).count();
    const double avg_ms = elapsed_ms / static_cast<double>(args.frames);
    const double fps = 1000.0 / avg_ms;

    fmt::print(
        "bench {}: frames={} warmup={} elapsed_ms={:.3f} avg_ms={:.3f} fps={:.2f}\n",
        args.comp_id, args.frames, args.warmup, elapsed_ms, avg_ms, fps);
    return 0;
}

} // namespace cli
} // namespace chronon3d
