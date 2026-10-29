#include "../../commands.hpp"
#include "../../command_registry.hpp"
#include "../../utils/common/cli_mappers.hpp"
#include "../../utils/job/cli_render_utils.hpp"
#include "bake_layer_cache.hpp"
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/compiler/compiler_pass_pipeline.hpp>
#include <chronon3d/compiler/compiler.hpp>
#include "compiler_graph.hpp"
#include <chronon3d/render_graph/executor/graph_executor.hpp>
#include <chronon3d/core/scope/execution_scope.hpp>
#include <chronon3d/render_graph/pipeline/graph_filter.hpp>
#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/runtime/job/job_session.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d::cli {

namespace {
struct BakeLayerState { std::shared_ptr<BakeLayerArgs> args{std::make_shared<BakeLayerArgs>()}; };
}

int command_bake_layer(const CompositionRegistry& registry, const BakeLayerArgs& args) {
    if (args.backend != "auto" && args.backend != "software" &&
        args.backend != "vulkan") {
        spdlog::error("[bake-layer] Invalid backend '{}'; expected auto, software, or vulkan",
                      args.backend);
        return 1;
    }
    auto resolved = resolve_composition(registry, args.comp_id);
    if (!resolved) {
        spdlog::error("[bake-layer] Unknown composition: {}", args.comp_id);
        return 1;
    }
    const auto& comp = *resolved.comp;

    RenderSettings settings;
    settings.text_layout_debug = args.diagnostic_overlay || args.diagnostic_overlay_only;
    settings.diagnostic_overlay_only = args.diagnostic_overlay_only;
    const auto assets_root = args.assets_root.empty()
        ? std::optional<std::filesystem::path>{}
        : std::optional<std::filesystem::path>{args.assets_root};
    Config renderer_config = Config::from_environment();
    renderer_config.set_backend_preference(parse_backend_preference(args.backend));
    auto renderer = create_renderer(
        registry, settings, std::move(renderer_config), assets_root);
    auto& runtime = *renderer->execution_context().runtime;

    const auto frame = static_cast<Frame>(args.frame);
    auto scene = comp.evaluate(make_frame_context({
        .global_time = SampleTime::from_frame_int(frame, comp.frame_rate()),
        .duration = comp.duration(),
        .width = comp.width(),
        .height = comp.height(),
        .assets_root = runtime.resolver().mount_root().string(),
        .runtime = &runtime,
    }));

    // Build the full render graph for the scene
    graph::RenderGraphContext graph_ctx;
    graph_ctx.frame_input.frame = frame;
    graph_ctx.frame_input.width = comp.width();
    graph_ctx.frame_input.height = comp.height();
    graph_ctx.frame_input.camera = Camera{};
    graph_ctx.backend = &renderer->backend();
    graph_ctx.node_cache = &renderer->node_cache();
    graph_ctx.framebuffer_pool = renderer->framebuffer_pool();
    graph_ctx.node_exec.counters = renderer->counters();
    graph_ctx.registry = &registry;

    compiler::CompilerPassPipeline graph_pipeline;
    graph_pipeline.add_default_passes();
    auto graph = graph_pipeline.build(scene, graph_ctx);

    // Select only the nodes belonging to the requested layer
    auto selection = graph::find_layer_nodes(graph, args.layer_id);

    if (selection.selected_output == graph::k_invalid_node) {
        spdlog::error("[bake-layer] Layer '{}' not found in composition '{}'",
                      args.layer_id, args.comp_id);
        return 1;
    }

    // P3 bake-once / composite-many: resolve the content-addressed cache
    // entry BEFORE the graph is moved into the compiler. The node digest is
    // the engine's own content identity (including the effective frame for
    // temporal nodes); the format, diagnostic and asset-tree inputs are folded
    // in beside it.
    std::optional<BakeLayerCache> bake_cache;
    std::string cache_entry_name;
    if (!args.cache_dir.empty()) {
        BakeLayerCacheKeyInput cache_input;
        cache_input.node_content_digest =
            graph.node(selection.selected_output).cache_key(graph_ctx).digest();
        cache_input.width = comp.width();
        cache_input.height = comp.height();
        cache_input.exr_bake = args.exr_bake;
        cache_input.diagnostic_overlay = args.diagnostic_overlay;
        cache_input.diagnostic_overlay_only = args.diagnostic_overlay_only;
        cache_input.assets_fingerprint =
            assets_tree_fingerprint(runtime.resolver().mount_root());
        cache_input.comp_id = args.comp_id;
        cache_input.layer_id = args.layer_id;
        bake_cache.emplace(args.cache_dir);
        cache_entry_name = bake_layer_cache_filename(cache_input);
        if (const auto hit = bake_cache->lookup(cache_entry_name)) {
            if (!bake_cache->materialize(*hit, args.output)) {
                spdlog::error("[bake-layer] Failed to materialize cached bake to {}",
                              args.output);
                return 1;
            }
            if (!args.quiet) {
                spdlog::info("[bake-layer] Cache hit {}: layer '{}' frame {} → {} "
                             "(baked once, composited without re-render)",
                             cache_entry_name, args.layer_id, frame, args.output);
            }
            return 0;
        }
    }

    if (!args.quiet) {
        spdlog::info("[bake-layer] Baking layer '{}' ({} matching nodes) frame {} → {}",
                     args.layer_id, selection.matching_nodes.size(), frame, args.output);
    }

    // Execute the graph for just the selected layer.
    // PR-2 rewire close-out (WP-8 followup) — retarget the graph's output
    // to the selected layer node, then compile through the compiler root
    // (single source of truth for topological plans after WP-8 close-out).
    //
    // Section 5 fix: route the executor via the renderer's runtime — the
    // executor is engine-lifetime owned by RuntimeContext (per AGENTS.md +
    // STATUS.md).  No local executor instantiation in CLI TUs.
    graph.retarget_output(selection.selected_output);
    auto compiled = chronon3d::compiler::detail::compile_graph(
        std::move(graph), graph_ctx);
    runtime::JobSession session;
    ExecutionScheduler scheduler{SchedulerMode::Sequential, 1, false};
    // FASE 5 — ExecutionScope public direct-ctor is closed; route
    // through make_root() with the session's own arena.
    //
    // This command is a one-shot bake with no leased frame yet, so use the
    // explicit slot-zero diagnostic arena. Production frame loops must go
    // through FrameExecution::arena().
    graph::ExecutionScope root_scope = graph::ExecutionScope::make_root(
        session, session.root_arena(), compiled.graph_instance_id);
    auto& executor = *renderer->execution_context().executor; // Section 5 fix.
    auto fb = executor.execute(
        compiled, graph_ctx, root_scope, scheduler).framebuffer;

    if (!fb) {
        spdlog::error("[bake-layer] Bake failed: layer '{}' produced no framebuffer",
                      args.layer_id);
        return 1;
    }

    ImageWriteOptions write_options;
    if (args.exr_bake) {
        write_options.format = ImageFormat::Exr;
        write_options.exr_half_float = true;
        write_options.exr_tiled = true;
        write_options.exr_dwaa = true;
    } else {
        write_options.format = ImageFormat::Png;
        write_options.convert_png_to_srgb = true;
    }

    if (!save_image(*fb, args.output, write_options)) {
        spdlog::error("[bake-layer] Failed to write output: {}", args.output);
        return 1;
    }

    // Publish the fresh bake so the next identical request is a file copy.
    // A failed store is not an error: the bake succeeded, the cache simply
    // misses next time.
    if (bake_cache && !bake_cache->store_from(args.output, cache_entry_name) &&
        !args.quiet) {
        spdlog::warn("[bake-layer] Could not publish bake into cache {}",
                     bake_cache->root().string());
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
    auto* bake = app.add_subcommand("bake-layer", "Bake a single static layer to an image");
    bake->add_option("comp", bake_args.comp_id, "Composition id")->required();
    bake->add_option("--assets-root", bake_args.assets_root,
                     "Root directory for resolving relative assets");
    bake->add_option("--layer", bake_args.layer_id, "Layer id to bake")->required();
    bake->add_option("--backend", bake_args.backend,
                     "Render backend: auto, software, or vulkan")
        ->default_val("auto");
    bake->add_option("--frame", bake_args.frame, "Frame to bake")->default_val(0);
    bake->add_option("-o,--output", bake_args.output, "Output image path")->required();
    bake->add_flag("--quiet", bake_args.quiet, "Suppress informational messages");
    bake->add_flag("--diagnostic", bake_args.diagnostic, "Enable diagnostic overlays");
    bake->add_flag("--diagnostic-overlay", bake_args.diagnostic_overlay,
                  "Draw diagnostic overlay on text layers: bbox, anchor point, baseline");
    bake->add_flag("--diagnostic-overlay-only", bake_args.diagnostic_overlay_only,
                  "Like --diagnostic-overlay but on transparent background (no scene content)");
    bake->add_flag("--exr-bake", bake_args.exr_bake, "Bake layer as OpenEXR with DWAA compression (linear 16-bit float)");
    bake->add_option("--cache-dir", bake_args.cache_dir,
                     "Bake-once/composite-many cache root: repeated identical bakes "
                     "are served from here without re-rendering");
    bake->callback([state, &ctx]() {
        ctx.exit_code = command_bake_layer(ctx.registry, *state->args);
    });
}

} // namespace chronon3d::cli
