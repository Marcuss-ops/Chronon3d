#include "../commands.hpp"
#include "../utils/cli_render_utils.hpp"
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>

namespace chronon3d {
namespace cli {

int command_graph(const CompositionRegistry& registry, const GraphArgs& args) {
    if (!registry.contains(args.comp_id)) {
        spdlog::error("Unknown composition: {}", args.comp_id);
        return 1;
    }

    auto comp = registry.create(args.comp_id);
    auto scene = comp.evaluate(args.frame);
    auto renderer = create_renderer(registry, RenderSettings{});
    const std::string dot = renderer->debug_render_graph(scene, comp.camera, comp.width(), comp.height(), args.frame);

    const std::filesystem::path out_path(args.output);
    if (out_path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(out_path.parent_path(), ec);
        if (ec) {
            spdlog::error("Cannot create output directory {}: {}", out_path.parent_path().string(), ec.message());
            return 1;
        }
    }

    std::ofstream out(args.output, std::ios::binary | std::ios::trunc);
    if (!out) {
        spdlog::error("Cannot open output file: {}", args.output);
        return 1;
    }

    out << dot;
    if (!out) {
        spdlog::error("Failed to write graph DOT to {}", args.output);
        return 1;
    }

    spdlog::info("Graph DOT saved to {}", args.output);
    return 0;
}

} // namespace cli
} // namespace chronon3d
