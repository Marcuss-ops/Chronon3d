#include "graph_builder_source_pass.hpp"
#include "../graph_builder_coordinates.hpp"

#include <chronon3d/render_graph/nodes/basic_nodes_common.hpp>
#include <chronon3d/render_graph/nodes/video_node.hpp>
#include <chronon3d/render_graph/nodes/text_run_node.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/render_graph/registry/graph_node_catalog.hpp>
#include <chronon3d/render_graph/registry/graph_node_create_request.hpp>
#include <chronon3d/internal/render_graph/cache/scene_program_store.hpp>
#include <memory>
#include <spdlog/spdlog.h>

namespace chronon3d::graph::detail {

using namespace chronon3d::graph;

GraphNodeId append_source_pass(RenderGraph& graph, const LayerGraphItem& item,
                               const RenderGraphContext& ctx) {
    const Layer& layer = *item.layer;
    const bool is_static = layer.cache_static || item.is_static;

    if (layer.kind == LayerKind::Adjustment) {
        return k_invalid_node;
    }

    if (layer.kind == LayerKind::Normal || layer.kind == LayerKind::Shape || layer.kind == LayerKind::Text) {
        if (layer.nodes.empty()) {
            return graph.add_node(std::make_unique<ClearNode>());
        }

        const bool layer_needs_transform = layer_needs_render_transform(item, ctx);
        const bool use_local = ctx.policy.modular_coordinates && layer_needs_transform && !item.native_3d;
        const bool source_is_static = is_static || use_local;

        if (ctx.policy.diagnostics_enabled) {
            spdlog::info(
                "[source-pass] layer='{}' kind={} item_transform_any={} implicit_center_only={} custom_transform={} use_local={} centered={} tx={} ty={}",
                layer.name.c_str(),
                static_cast<int>(layer.kind),
                item.transform.any(),
                is_implicit_2d_centering_only(item, ctx),
                has_custom_render_transform(item, ctx),
                use_local,
                should_use_centered_rendering(item, ctx),
                item.transform.position.x,
                item.transform.position.y
            );
        }

        const Mat4 item_source_world = use_local
            ? item.world_matrix
            : source_space_world_matrix(item, ctx);

        if (layer.nodes.size() == 1) {
            const auto& node = layer.nodes[0];
            const u64 content_hash = hash_render_node_content_only(node);
            const u64 placement_hash = hash_render_node_placement_only(node);
            GraphNodeId source;

            // ── TextRun branch ─────────────────────────────────────────
            // If the source RenderNode has ShapeType::TextRun
            // (set by LayerBuilder::text_run()), route to a TextRunNode
            // instead of a SourceNode.  Only single-node layers are
            // supported here — multi-node aggregation into MultiSourceNode
            // does not currently understand TextRunShape.
            if (node.shape.type() == ShapeType::TextRun) {
                auto run_shape = node.shape.text_run_shape_handle().value;
                if (!run_shape) {
                    // Hard fail — null shape is a wiring error (wiring failed
                    // to attach the shape).  Don't silently fall through to
                    // SourceNode and render a blank layer; the user must fix
                    // the binding (LayerBuilder::text_run + materialize_text_run_shape).
                    throw std::logic_error(
                        "[source-pass] layer='" + std::string(layer.name) + "' node='" + std::string(node.name) +
                        "' ShapeType::TextRun but text_run_shape_handle().value is null — "
                        "wiring failed to attach the shape. Check LayerBuilder::text_run() + "
                        "materialize_text_run_shape().");
                }
                cache::NodeCacheKey run_key{
                    .scope = "layer.textrun:" + std::string(layer.name) + ":" + std::string(node.name),
                    .frame = source_is_static ? Frame{0} : ctx.frame_input.frame,
                    .width = ctx.frame_input.width,
                    .height = ctx.frame_input.height,
                    .params_hash = content_hash,
                    .source_hash = hash_combine(hash_string(node.name), placement_hash)
                };
                // TICKET-ae-cam-hash-collision Soluzione B
                if (ctx.frame_input.has_camera_2_5d) {
                    cache::fold_camera_into_params_hash(run_key, ctx.frame_input.camera_2_5d);
                }
                const Mat4 run_matrix = use_local
                    ? node.world_transform.to_mat4()
                    : (item_source_world * node.world_transform.to_mat4());
                const f32 run_opacity = use_local
                    ? node.world_transform.opacity
                    : (item.transform.opacity * node.world_transform.opacity);                    // TICKET-TEXT-CLEANUP-5: always pass resolved matrix.
                    // Canvas center is now baked into matrix_override by the
                    // source pass — TextRunNode no longer decides centering.
                    Mat4 resolved_matrix = run_matrix;
                    f32  resolved_opacity = run_opacity;
                    if (!ctx.policy.modular_coordinates
                        && should_use_centered_rendering(item, ctx)) {
                        resolved_matrix = implicit_canvas_center_matrix(ctx) * run_matrix;
                    }
                    source = graph.add_node(std::make_unique<TextRunNode>(
                        std::string(node.name),
                        run_shape,
                        node,
                        run_key,
                        item.projected,
                        std::optional<Mat4>(resolved_matrix),
                        std::optional<f32>(resolved_opacity),
                        source_is_static ? static_memory_cache("text_run") : frame_variant_cache("text_run")
                    ));

                if (ctx.policy.diagnostics_enabled) {
                    spdlog::info(
                        "[source-pass] layer='{}' routed to TextRunNode "
                        "glyphs={} centered={} projected={}",
                        layer.name.c_str(),
                        node.shape.text_run_shape_handle().value->glyphs.size(),
                        should_use_centered_rendering(item, ctx),
                        item.projected
                    );
                }
                return source;
            }

            // ShapeType::Text is deprecated — all text now routes through
            // ShapeType::TextRun (set by LayerBuilder::text_run()).  If this
            // triggers, the caller is using the legacy text() API without
            // the TextRun migration.
            if (node.shape.type() == ShapeType::Text) {
                throw std::logic_error(
                    "[source-pass] ShapeType::Text is deprecated; use TextRun. "
                    "LayerBuilder::text() should be migrated to text_run(). "
                    "layer='" + std::string(layer.name) + "' node='" + std::string(node.name) + "'");
            }
            {
                cache::NodeCacheKey source_key{
                    .scope = "layer.source:" + std::string(layer.name) + ":" + std::string(node.name),
                    .frame = source_is_static ? Frame{0} : ctx.frame_input.frame,
                    .width = ctx.frame_input.width,
                    .height = ctx.frame_input.height,
                    .params_hash = content_hash,
                    .source_hash = hash_combine(hash_string(node.name), placement_hash)
                };
                // TICKET-ae-cam-hash-collision Soluzione B
                if (ctx.frame_input.has_camera_2_5d) {
                    cache::fold_camera_into_params_hash(source_key, ctx.frame_input.camera_2_5d);
                }

                const Mat4 shape_matrix = use_local
                    ? node.world_transform.to_mat4()
                    : (item_source_world * node.world_transform.to_mat4());
                const f32 shape_opacity = use_local
                    ? node.world_transform.opacity
                    : (item.transform.opacity * node.world_transform.opacity);

                source = graph.add_node(std::make_unique<SourceNode>(
                    std::string(node.name), node, source_key,
                    should_use_centered_rendering(item, ctx),
                    item.projected,
                    ctx.policy.modular_coordinates ? std::optional<Mat4>(shape_matrix) : std::nullopt,
                    ctx.policy.modular_coordinates ? std::optional<f32>(shape_opacity) : std::nullopt,
                    source_is_static ? static_memory_cache("source") : frame_variant_cache("source")
                ));
            }
            return source;
        }

        // Build an aggregated cache key.
        //
        // MultiSourceNode understands text_run-flagged nodes —
        // it dispatches them to `renderer::draw_text_run` (via
        // SoftwareRenderer dynamic_cast) inside `execute()`.  No warning
        // is emitted: text and shapes coexist in the same layer, in
        // vector order, and composite onto the shared framebuffer.
        //
        // The `hash_text_run_shape(*shape)` fold is intentionally NOT
        // added here — `MultiSourceNode::cache_key()` re-folds it per
        // item at evaluation time so animator mutations invalidate the
        // entry per-frame.  Folding it once at build time would also
        // work (with refresh as fallback) but doubles the bytes hashed
        // each evaluation.  Single source of truth lives in `cache_key`.
        //
        // Orphan guard (parity with the single-source path's per-item
        // wiring-error log): if any TextRun-typed node lacks an
        // attached shape, surface it ONCE per layer so multi-source
        // errors aren't silent.
        u64 aggregated_params_hash = 0;
        u64 aggregated_source_hash = hash_string(std::string(layer.name) + "_multisource");
        bool saw_orphan_text_run = false;
        for (const auto& node : layer.nodes) {
            if (node.shape.type() == ShapeType::TextRun
                && !node.shape.text_run_shape_handle().value) {
                saw_orphan_text_run = true;
            }
            aggregated_params_hash = hash_combine(aggregated_params_hash, hash_render_node_content_only(node));
            aggregated_source_hash = hash_combine(
                aggregated_source_hash,
                hash_combine(hash_string(node.name), hash_render_node_placement_only(node))
            );
        }
        if (saw_orphan_text_run) {
            spdlog::error(
                "[source-pass] layer='{}' contains a TextRun-typed node "
                "with null text_run_shape_handle().value in a multi-node layer — the text "
                "will be skipped at execute time.  Wiring failed to "
                "attach the shape; check LayerBuilder::text_run + "
                "materialize_text_run_shape.",
                layer.name.c_str());
        }

        cache::NodeCacheKey source_key{
            .scope = "layer.multisource:" + std::string(layer.name),
            .frame = source_is_static ? Frame{0} : ctx.frame_input.frame,
            .width = ctx.frame_input.width,
            .height = ctx.frame_input.height,
            .params_hash = aggregated_params_hash,
            .source_hash = aggregated_source_hash
        };
        // TICKET-ae-cam-hash-collision Soluzione B
        if (ctx.frame_input.has_camera_2_5d) {
            cache::fold_camera_into_params_hash(source_key, ctx.frame_input.camera_2_5d);
        }

        std::vector<MultiSourceItem> items;
        items.reserve(layer.nodes.size());

        // Items are pushed unconditionally — even TextRun-typed nodes —
        // because MultiSourceNode::execute() dispatches on
        // `item.node->shape.type() == ShapeType::TextRun` per item.  Order is the
        // layer.nodes vector order, so later items composite SRC_OVER
        // earlier ones on the shared framebuffer (matches pre-PR-6
        // behaviour for non-text items).
        for (const auto& node : layer.nodes) {
            const Mat4 shape_matrix = use_local
                ? node.world_transform.to_mat4()
                : (item_source_world * node.world_transform.to_mat4());
            const f32 shape_opacity = use_local
                ? node.world_transform.opacity
                : (item.transform.opacity * node.world_transform.opacity);

            items.push_back(MultiSourceItem{
                .node = &node,
                .matrix = shape_matrix,
                .opacity = shape_opacity
            });
        }

        auto multi_source = graph.add_node(std::make_unique<MultiSourceNode>(
            std::string(layer.name) + "_multi",
            std::move(items),
            source_key,
            should_use_centered_rendering(item, ctx),
            item.projected,
            source_is_static ? static_memory_cache("multi_source") : frame_variant_cache("multi_source")
        ));
        return multi_source;
    }

    if (layer.kind == LayerKind::Precomp) {
        const size_t cache_cap   = ctx.policy.program_cache_capacity > 0
            ? ctx.policy.program_cache_capacity
            : 8;  // default
        const auto tune_mode     = ctx.policy.program_cache_tune
            ? cache::TuneMode::Auto
            : cache::TuneMode::Fixed;

        // Create PrecompNode via GraphNodeCatalog to break the
        // graph_builder → graph_pipeline CMake cycle.
        // PR-5 — cache config is now a PrecompCachePolicy.
        PrecompCachePolicy cache_policy{
            .initial_capacity = cache_cap,
            .mode = tune_mode,
            .tuning = cache::TuneConfig{
                .interval     = ctx.policy.program_cache_tune_interval,
                .min_capacity = ctx.policy.program_cache_tune_min_capacity,
                .max_capacity = ctx.policy.program_cache_tune_max_capacity,
            }
        };
        GraphNodeCreateRequest request{
            .payload = PrecompNodeCreateSpec{
                .composition_name =
                    std::string(layer.precomp_composition_name),
                .start_frame = layer.from,
                .duration = layer.duration,
                .cache_frame = is_static ? Frame{0} : Frame{-1},
                .cache_policy = cache_policy,
            }
        };

        if (!ctx.services.node_catalog) {
            throw std::logic_error(
                "source.precomp: node_catalog not wired (call wire_precomp_build_factory)");
        }
        auto node = ctx.services.node_catalog->create(
            "source.precomp", request);

        if (!node) {
            throw std::logic_error(
                "source.precomp factory is not registered");
        }

        // PR2-cleanup: precomp cache policy is fixed at the catalog factory
        // (PrecompNode is catalog-constructed, no in-place mutation path).
        return graph.add_node(std::move(node));
    }

    if (layer.kind == LayerKind::Video && layer.video_source) {
        // PR2-cleanup: VideoNode is intrinsically frame-variant per its ctor;
        // the cache policy is baked in by the factory.
        return graph.add_node(std::make_unique<VideoNode>(
            *layer.video_source, ctx.services.video_decoder, layer.from
        ));
    }

    return graph.add_node(std::make_unique<ClearNode>());
}

} // namespace chronon3d::graph::detail
