#include "graph_builder_source_pass.hpp"
#include "../graph_builder_coordinates.hpp"
#include "../evaluated_layer_placement.hpp"

#include <chronon3d/cache/node_cache_identity_builder.hpp>

#include <chronon3d/render_graph/nodes/basic_nodes_common.hpp>
#include <chronon3d/render_graph/nodes/video_node.hpp>
#include <chronon3d/render_graph/nodes/text_run_node.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/render_graph/registry/graph_node_catalog.hpp>
#include <chronon3d/render_graph/registry/graph_node_create_request.hpp>
#include <chronon3d/runtime/resource_preparation.hpp>
#include <chronon3d/internal/render_graph/cache/scene_program_store.hpp>
#include <stdexcept>
#include <memory>
#include <spdlog/spdlog.h>

namespace chronon3d::graph::detail {

using namespace chronon3d::graph;

::chronon3d::RenderNode materialize_mesh_node(
    const ::chronon3d::RenderNode& node,
    const RenderGraphContext& ctx) {
    if (node.shape.type() != ShapeType::Mesh) return node;

    auto materialized = node;
    auto& mesh_shape = materialized.shape.mesh_shape();
    if (mesh_shape.prepared || mesh_shape.mesh) {
        return materialized;
    }
    if (mesh_shape.reference.path().empty()) {
        if (mesh_shape.reference.required()) {
            throw std::runtime_error(
                "Mesh source preparation failed: required mesh reference has an empty path");
        }
        return materialized;
    }
    if (!ctx.services.prepared_assets) {
        throw std::runtime_error(
            "Mesh source cannot be materialized: matching preparation snapshot is not wired");
    }

    // Graph construction consumes only the immutable source populated by the
    // preparation barrier. It must never resolve paths or invoke MeshLoader.
    const auto prepared = ctx.services.prepared_assets->meshes.find(
        mesh_shape.reference.owner());
    if (prepared == ctx.services.prepared_assets->meshes.end() ||
        prepared->second.path != mesh_shape.reference.path() ||
        !prepared->second.source) {
        throw std::runtime_error(
            "Mesh source was not prepared before RenderGraph build: "
            + mesh_shape.reference.path());
    }
    mesh_shape.prepared = prepared->second.source;
    return materialized;
}

GraphNodeId append_source_pass(RenderGraph& graph, const LayerGraphItem& item,
                               const RenderGraphContext& ctx,
                               const BuilderContext& node_ctx) {
    const Layer& layer = *item.layer;
    const bool is_static = layer.cache_static || item.is_static;

    if (layer.kind == LayerKind::Adjustment) {
        return k_invalid_node;
    }

    if (layer.kind == LayerKind::Normal || layer.kind == LayerKind::Shape || layer.kind == LayerKind::Text) {
        if (layer.nodes.empty()) {
            return graph.add_node(std::make_unique<ClearNode>(), node_ctx);
        }

        const auto placement = evaluate_layer_placement(item, ctx);
        // A projected item is transformed by the downstream TransformNode.
        // Keep its source local; including the resolved layer world matrix
        // here would apply the layer translation twice.
        const bool use_local = placement.space == EvaluatedCoordinateSpace::Local;
        // A local transform changes the rasterized placement, but it does not
        // make an animated layer's source frame-invariant.  Treating
        // `use_local` as static reused the first moving shape in dirty/tile
        // sequences and left the old circle at the previous position.
        const bool source_is_static = is_static;

        if (ctx.policy.diagnostics_enabled) {
            spdlog::debug(
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

        // The resolver owns source-stage matrix selection. Projected
        // TransformNode input is canvas-space; native 3D keeps processor
        // camera ownership; local and canvas paths retain their existing
        // source matrices.
        if (layer.nodes.size() == 1) {
            const auto& authored_node = layer.nodes[0];
            const auto node = materialize_mesh_node(authored_node, ctx);
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
                    // A null shape is a controlled materialization failure
                    // (missing/corrupt font or empty content), not a graph
                    // construction failure. Preserve the diagnostic while
                    // returning a transparent frame so callers can inspect
                    // the zero-glyph result without an exception.
                    spdlog::error(
                        "[source-pass] layer='{}' node='{}' TextRun materialization produced no shape",
                        layer.name.c_str(), node.name.c_str());
                    return graph.add_node(std::make_unique<ClearNode>(), node_ctx);
                }
                // Resolve the placement before finalizing the cache key. A
                // tight projected TextRun is a local raster surface; camera
                // pose and projected matrix belong to the downstream
                // TransformNode and must not create one raster-cache entry
                // per camera frame.
                f32 resolved_opacity = 0.0f;
                auto placement = resolve_text_run_placement(item, node, ctx, resolved_opacity);

                // Canonical cache identity (TICKET-ae-cam-hash-collision
                // Soluzione B). Tight-surface text is rasterized in a local
                // surface owned downstream by TransformNode, so camera state
                // must NOT fragment this key — camera_if(false) keeps parity.
                cache::NodeCacheKey run_key = cache::NodeCacheIdentityBuilder{
                    "layer.textrun:" + std::string(layer.name) + ":" +
                    std::string(node.name)
                }
                    .frame(source_is_static ? Frame{0} : ctx.frame_input.frame)
                    .output(ctx.frame_input.width, ctx.frame_input.height)
                    .params(content_hash)
                    .source(hash_combine(hash_string(node.name), placement_hash))
                    .camera_if(ctx.frame_input.has_camera_2_5d &&
                                   !placement.tight_surface,
                               ctx.frame_input.camera_2_5d)
                    .build();

                source = graph.add_node(std::make_unique<TextRunNode>(
                    std::string(node.name),
                    std::string(layer.name),
                    run_shape,
                    node,
                    run_key,
                    placement,
                    std::optional<f32>(resolved_opacity),
                    source_is_static ? static_memory_cache("text_run") : frame_variant_cache("text_run")
                ), node_ctx);

                if (ctx.policy.diagnostics_enabled) {
                    spdlog::info(
                        "[source-pass] layer='{}' routed to TextRunNode glyphs={}",
                        layer.name.c_str(),
                        node.shape.text_run_shape_handle().value->glyphs.size()
                    );
                }
                return source;
            }

            {
                // Canonical cache identity (TICKET-ae-cam-hash-collision
                // Soluzione B): camera folded by construction.
                cache::NodeCacheKey source_key = cache::NodeCacheIdentityBuilder{
                    "layer.source:" + std::string(layer.name) + ":" +
                    std::string(node.name)
                }
                    .frame(source_is_static ? Frame{0} : ctx.frame_input.frame)
                    .output(ctx.frame_input.width, ctx.frame_input.height)
                    .params(content_hash)
                    .source(hash_combine(hash_string(node.name), placement_hash))
                    .camera_if(ctx.frame_input.has_camera_2_5d,
                               ctx.frame_input.camera_2_5d)
                    .build();

                const auto source_placement = evaluate_source_placement(item, node, ctx);
                const Mat4 shape_matrix = finalize_source_placement_matrix(
                    source_placement, item, node, ctx);
                const f32 shape_opacity = source_placement.opacity;

                source = graph.add_node(std::make_unique<SourceNode>(
                    std::string(node.name), node, source_key,
                    std::optional<Mat4>(shape_matrix),
                    std::optional<f32>(shape_opacity),
                    source_is_static ? static_memory_cache("source") : frame_variant_cache("source"),
                    !item.layer->screen_space,
                    item.projected && !item.native_3d && !item.layer->screen_space,
                    item.native_3d
                ), node_ctx);
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
        for (const auto& authored_node : layer.nodes) {
            const auto node = materialize_mesh_node(authored_node, ctx);
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
            throw std::logic_error(
                "[source-pass] layer='" + std::string(layer.name) + "' contains a TextRun-typed node "
                "with null text_run_shape_handle().value in a multi-node layer. "
                "Wiring failed to attach the shape; check LayerBuilder::text_run() + "
                "materialize_prepared_text().");
        }

        // Canonical cache identity (TICKET-ae-cam-hash-collision Soluzione
        // B): camera folded by construction.
        cache::NodeCacheKey source_key = cache::NodeCacheIdentityBuilder{
            "layer.multisource:" + std::string(layer.name)
        }
            .frame(source_is_static ? Frame{0} : ctx.frame_input.frame)
            .output(ctx.frame_input.width, ctx.frame_input.height)
            .params(aggregated_params_hash)
            .source(aggregated_source_hash)
            .camera_if(ctx.frame_input.has_camera_2_5d,
                       ctx.frame_input.camera_2_5d)
            .build();

        std::vector<RenderNode> materialized_nodes;
        materialized_nodes.reserve(layer.nodes.size());
        for (const auto& authored_node : layer.nodes) {
            materialized_nodes.push_back(materialize_mesh_node(authored_node, ctx));
        }
        std::vector<MultiSourceItem> items;
        items.reserve(materialized_nodes.size());

        // Items are pushed unconditionally — even TextRun-typed nodes —
        // because MultiSourceNode::execute() dispatches on
        // `item.node->shape.type() == ShapeType::TextRun` per item.  Order is the
        // layer.nodes vector order, so later items composite SRC_OVER
        // earlier ones on the shared framebuffer (matches pre-PR-6
        // behaviour for non-text items).
        for (const auto& node : materialized_nodes) {
            const auto source_placement = evaluate_source_placement(item, node, ctx);
            const Mat4 shape_matrix = finalize_source_placement_matrix(
                source_placement, item, node, ctx);
            const f32 shape_opacity = source_placement.opacity;

            items.push_back(MultiSourceItem{
                .node = &node,
                .matrix = shape_matrix,
                .opacity = shape_opacity,
                .defer_camera_projection = item.projected && !item.native_3d &&
                    !item.layer->screen_space,
                .apply_camera_projection = !item.layer->screen_space,
                .native_3d = item.native_3d,
            });
        }

        auto multi_source = graph.add_node(std::make_unique<MultiSourceNode>(
            std::string(layer.name) + "_multi",
            std::move(items),
            source_key,
            source_is_static ? static_memory_cache("multi_source") : frame_variant_cache("multi_source")
        ), node_ctx);
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
        return graph.add_node(std::move(node), node_ctx);
    }

    if (layer.kind == LayerKind::Video && layer.video_source) {
        // PR2-cleanup: VideoNode is intrinsically frame-variant per its ctor;
        // the cache policy is baked in by the factory.
        return graph.add_node(std::make_unique<VideoNode>(
            *layer.video_source, ctx.services.video_decoder, layer.from
        ), node_ctx);
    }

    return graph.add_node(std::make_unique<ClearNode>(), node_ctx);
}

} // namespace chronon3d::graph::detail
