#include "../commands.hpp"
#include "../utils/cli_render_utils.hpp"
#include <chronon3d/render_graph/render_pipeline.hpp>
#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>

namespace chronon3d {
namespace cli {

namespace {

std::string format_summary(const std::string& comp_id, Frame frame,
                            int width, int height,
                            const graph::SceneGraphStats& s) {
    std::string out;
    out += fmt::format("Graph summary: {} frame {}\n", comp_id, frame);
    out += fmt::format("Scene:\n");
    out += fmt::format("  layers:        {}\n", s.scene_layers);
    out += fmt::format("  width:         {}\n", width);
    out += fmt::format("  height:        {}\n", height);
    out += "\nGraph:\n";
    out += fmt::format("  nodes:         {}\n", s.total_nodes);
    if (s.output_id != graph::k_invalid_node)
        out += fmt::format("  output node:   {}\n", s.output_id);
    out += fmt::format("  sources:       {}\n", s.source_nodes);
    out += fmt::format("  transforms:    {}\n", s.transform_nodes);
    out += fmt::format("  effects:       {}\n", s.effect_nodes);
    out += fmt::format("  masks:         {}\n", s.mask_nodes);
    out += fmt::format("  composites:    {}\n", s.composite_nodes);
    out += fmt::format("  adjustments:   {}\n", s.adjustment_nodes);
    out += fmt::format("  precomps:      {}\n", s.precomp_nodes);
    out += fmt::format("  videos:        {}\n", s.video_nodes);
    out += fmt::format("  motion blur:   {}\n", s.motion_blur_nodes);
    out += fmt::format("  other:         {}\n", s.other_nodes);
    out += fmt::format("  cacheable:     {}\n", s.cacheable_nodes);

    if (s.execute_ms > 0.0) {
        const size_t hits    = s.cache_after.hits      - s.cache_before.hits;
        const size_t misses  = s.cache_after.misses    - s.cache_before.misses;
        const size_t evicted = s.cache_after.evictions - s.cache_before.evictions;
        const double mem_mb  = static_cast<double>(s.cache_after.current_usage_bytes) / (1024.0 * 1024.0);
        out += "\nCache:\n";
        out += fmt::format("  hits:          {}\n", hits);
        out += fmt::format("  misses:        {}\n", misses);
        out += fmt::format("  evictions:     {}\n", evicted);
        out += fmt::format("  memory:        {:.1f} MB\n", mem_mb);
    }

    out += "\nTiming:\n";
    out += fmt::format("  graph_build:   {:.2f} ms\n", s.build_ms);
    if (s.execute_ms > 0.0)
        out += fmt::format("  graph_exec:    {:.2f} ms\n", s.execute_ms);
    out += fmt::format("  total:         {:.2f} ms\n", s.total_ms);
    return out;
}

bool write_dot(const std::string& dot, const std::string& path) {
    std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
        if (ec) {
            spdlog::error("[graph] Cannot create directory {}: {}", p.parent_path().string(), ec.message());
            return false;
        }
    }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f || !(f << dot)) {
        spdlog::error("[graph] Cannot write DOT to {}", path);
        return false;
    }
    spdlog::info("[graph] DOT saved to {}", path);
    return true;
}

} // namespace

int command_graph(const CompositionRegistry& registry, const GraphArgs& args) {
    auto resolved = resolve_composition(registry, args.comp_id);
    if (!resolved) return 1;

    const auto& comp = *resolved.comp;
    auto scene = comp.evaluate(args.frame);

    RenderSettings settings;
    settings.use_modular_graph = true;
    auto renderer = create_renderer(registry, settings);

    if (args.summary) {
        const bool need_dot = !args.output.empty();
        const auto stats = graph::analyze_scene_graph(
            *renderer, renderer->node_cache(), scene,
            comp.camera, comp.width(), comp.height(),
            args.frame, 0.0f, settings, &registry, nullptr,
            /*execute=*/true, /*include_dot=*/need_dot
        );
        fmt::print("{}", format_summary(args.comp_id, args.frame, comp.width(), comp.height(), stats));
        if (need_dot && !write_dot(stats.dot, args.output)) return 1;
        return 0;
    }

    // Legacy: export DOT only
    const std::string dot_path = args.output.empty() ? "output/graph.dot" : args.output;
    const auto stats = graph::analyze_scene_graph(
        *renderer, renderer->node_cache(), scene,
        comp.camera, comp.width(), comp.height(),
        args.frame, 0.0f, settings, &registry, nullptr,
        /*execute=*/false, /*include_dot=*/true
    );
    return write_dot(stats.dot, dot_path) ? 0 : 1;
}

} // namespace cli
} // namespace chronon3d
