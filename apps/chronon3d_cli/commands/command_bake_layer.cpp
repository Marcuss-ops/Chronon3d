#include "../commands.hpp"
#include "../command_registry.hpp"
#include "../utils/cli_render_utils.hpp"
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/render_graph/graph_builder.hpp>
#include <chronon3d/render_graph/graph_executor.hpp>
#include <chronon3d/render_graph/graph_filter.hpp>
#include <chronon3d/core/composition_registry.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d::cli {

namespace {
struct BakeLayerState { std::shared_ptr<BakeLayerArgs> args{std::make_shared<BakeLayerArgs>()}; };
}

int command_bake_layer(const CompositionRegistry& registry, const BakeLayerArgs& args) {
    auto resolved = resolve_composition(registry, args.comp_id);
    if (!resolved) {
        spdlog::error("[bake-layer] Unknown composition: {}", args.comp_id);
        return 1;
    }
    const auto& comp = *resolved.comp;

    RenderSettings settings;
    settings.use_modular_graph = true;
    auto renderer = create_renderer(registry, settings);

    const auto frame = static_cast<Frame>(args.frame);
    auto scene = comp.evaluate(frame);

    // Build the full render graph for the scene
    graph::RenderGraphContext graph_ctx;
    graph_ctx.frame = frame;
    graph_ctx.width = comp.width();
    graph_ctx.height = comp.height();
    graph_ctx.camera = comp.camera;
    graph_ctx.backend = renderer.get();
    graph_ctx.node_cache = &renderer->node_cache();
    graph_ctx.framebuffer_pool = renderer->framebuffer_pool();
    graph_ctx.trace = renderer->trace();
    graph_ctx.counters = renderer->counters();
    graph_ctx.registry = &registry;

    auto graph = graph::GraphBuilder::build(scene, graph_ctx);

    // Select only the nodes belonging to the requested layer
    auto selection = graph::find_layer_nodes(graph, args.layer_id);

    if (selection.selected_output == graph::k_invalid_node) {
        spdlog::error("[bake-layer] Layer '{}' not found in composition '{}'",
                      args.layer_id, args.comp_id);
        return 1;
    }

    if (!args.quiet) {
        spdlog::info("[bake-layer] Baking layer '{}' ({} matching nodes) frame {} → {}",
                     args.layer_id, selection.matching_nodes.size(), frame, args.output);
    }

    // Execute the graph for just the selected layer
    graph::GraphExecutor executor;
    auto fb = executor.execute(graph, selection.selected_output, graph_ctx);

    if (!fb) {
        spdlog::error("[bake-layer] Bake failed: layer '{}' produced no framebuffer",
                      args.layer_id);
        return 1;
    }

    ImageWriteOptions write_options;
    write_options.format = ImageFormat::Png;
    write_options.convert_png_to_srgb = true;

    if (!save_image(*fb, args.output, write_options)) {
        spdlog::error("[bake-layer] Failed to write output: {}", args.output);
        return 1;
    }

    if (!args.quiet) {
        spdlog::info("[bake-layer] Baked layer '{}' to {} ({}x{})",
                     args.layer_id, args.output, fb->width(), fb->height());
    }

    return 0;
}

void register_bake_layer_commands(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<BakeLayerState>();
    auto& bake_args = *state->args;
    auto* bake = app.add_subcommand("bake-layer", "Bake a single static layer to a PNG image");
    bake->add_option("comp", bake_args.comp_id, "Composition id")->required();
    bake->add_option("--layer", bake_args.layer_id, "Layer id to bake")->required();
    bake->add_option("--frame", bake_args.frame, "Frame to bake")->default_val(0);
    bake->add_option("-o,--output", bake_args.output, "Output PNG image path")->required();
    bake->add_flag("--quiet", bake_args.quiet, "Suppress informational messages");
    bake->add_flag("--diagnostic", bake_args.diagnostic, "Enable diagnostic overlays");
    bake->callback([state, &ctx]() {
        ctx.exit_code = command_bake_layer(ctx.registry, *state->args);
    });
}

} // namespace chronon3d::cli
