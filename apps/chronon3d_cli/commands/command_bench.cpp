#include "../commands.hpp"
#include "../utils/cli_render_utils.hpp"
#include <chronon3d/runtime/bench_runner.hpp>
#include <spdlog/spdlog.h>
#include <fmt/format.h>

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
    auto renderer = create_renderer(registry, RenderSettings{.use_modular_graph = args.use_modular_graph});

    spdlog::info("Benchmarking {} (warmup: {}, frames: {})", args.comp_id, args.warmup, args.frames);

    auto result = runtime::BenchRunner::run(args.comp_id, comp, *renderer, args.frames, args.warmup);

    fmt::print(
        "bench {}: frames={} warmup={} elapsed_ms={:.3f} avg_ms={:.3f} fps={:.2f}\n",
        result.comp_id, result.frames, result.warmup, result.total_elapsed_ms, result.avg_ms, result.fps);
    return 0;
}

} // namespace cli
} // namespace chronon3d
