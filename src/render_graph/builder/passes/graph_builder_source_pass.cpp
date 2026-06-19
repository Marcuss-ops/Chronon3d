#include "graph_builder_source_pass.hpp"
#include "../graph_builder_coordinates.hpp"

#include <chronon3d/render_graph/nodes/basic_nodes_common.hpp>
#include <chronon3d/render_graph/nodes/video_node.hpp>
#include <chronon3d/render_graph/nodes/text_run_node.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/render_graph/registry/graph_node_catalog.hpp>
#include <chronon3d/render_graph/registry/graph_node_create_request.hpp>
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

    if (layer.kind == LayerKind::Normal) {
        if (layer.nodes.empty()) {
            return graph.add_node(std::make_unique<ClearNode>());
        }

        const bool layer_needs_transform = layer_needs_render_transform(item, ctx);
        const bool use_local = ctx.options.modular_coordinates && layer_needs_transform && !item.native_3d;
        const bool source_is_static = is_static || use_local;

        if (ctx.options.diagnostics_enabled) {
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
            // If the source RenderNode was flagged as a TextRun shape
            // (set by LayerBuilder::text_run()), route to a TextRunNode
            // instead of a SourceNode.  Only single-node layers are
            // supported here — multi-node aggregation into MultiSourceNode
            // does not currently understand TextRunShape.
            if (node.is_text_run_shape) {
                // Defensive: a flagged text_run node with a null shape is a
                // programmer error (wiring failed to attach the shape).
                // Surface loudly so the user fixes the binding rather than
                // wondering why their text silently vanished.
                if (!node.text_run_shape) {
                    spdlog::error(
                        "[source-pass] layer='{}' node='{}' is_text_run_shape=true "
                        "but text_run_shape is null — wiring failed to attach "
                        "the shape; falling through to SourceNode path.",
                        layer.name.c_str(), std::string(node.name));
                } else {
                    cache::NodeCacheKey run_key{
                        .scope = "layer.textrun:" + std::string(layer.name) + ":" + std::string(node.name),
                        .frame = source_is_static ? Frame{0} : ctx.frame.frame,
                        .width = ctx.frame.width,
                        .height = ctx.frame.height,
                        .params_hash = content_hash,
                        .source_hash = hash_combine(hash_string(node.name), placement_hash)
                    };
                    const Mat4 run_matrix = use_local
                        ? node.world_transform.to_mat4()
                        : (item_source_world * node.world_transform.to_mat4());
                    const f32 run_opacity = use_local
                        ? node.world_transform.opacity
                        : (item.transform.opacity * node.world_transform.opacity);

                    source = graph.add_node(std::make_unique<TextRunNode>(
                        std::string(node.name),
                        node.text_run_shape,
                        node,
                        run_key,
                        should_use_centered_rendering(item, ctx),
                        item.projected,
                        ctx.options.modular_coordinates ? std::optional<Mat4>(run_matrix) : std::nullopt,
                        ctx.options.modular_coordinates ? std::optional<f32>(run_opacity) : std::nullopt,
                        source_is_static
                        // cache_policy is fixed inside TextRunNode ctor from source_is_static
                    ));

                    if (ctx.options.diagnostics_enabled) {
                        spdlog::info(
                            "[source-pass] layer='{}' routed to TextRunNode "
                            "glyphs={} centered={} projected={}",
                            layer.name.c_str(),
                            node.text_run_shape->glyphs.size(),
                            should_use_centered_rendering(item, ctx),
                            item.projected
                        );
                    }
                    return source;
                }
                // Null shape → fall through to existing SourceNode paths so the
                // composition still builds.  The error log above is the user
                // signal; the resulting SourceNode will render a blank layer.
            }

            if (node.shape.type == ShapeType::Text) {
                cache::NodeCacheKey source_key{
                    .scope = "layer.source:" + std::string(layer.name) + ":" + std::string(node.name),
                    .frame = source_is_static ? Frame{0} : ctx.frame.frame,
                    .width = ctx.frame.width,
                    .height = ctx.frame.height,
                    .params_hash = content_hash,
                    .source_hash = hash_combine(hash_string(node.name), placement_hash)
                };

                const Mat4 text_matrix = use_local
                    ? node.world_transform.to_mat4()
                    : (item_source_world * node.world_transform.to_mat4());
                const f32 text_opacity = use_local
                    ? node.world_transform.opacity
                    : (item.transform.opacity * node.world_transform.opacity);

                source = graph.add_node(std::make_unique<SourceNode>(
                    std::string(node.name), node, source_key,
                    should_use_centered_rendering(item, ctx),
                    item.projected,
                    ctx.options.modular_coordinates ? std::optional<Mat4>(text_matrix) : std::nullopt,
                    ctx.options.modular_coordinates ? std::optional<f32>(text_opacity) : std::nullopt,
                    source_is_static
                ));
                // SourceNode ctor sets cache_policy from source_is_static.
            } else {
                cache::NodeCacheKey source_key{
                    .scope = "layer.source:" + std::string(layer.name) + ":" + std::string(node.name),
                    .frame = source_is_static ? Frame{0} : ctx.frame.frame,
                    .width = ctx.frame.width,
                    .height = ctx.frame.height,
                    .params_hash = content_hash,
                    .source_hash = hash_combine(hash_string(node.name), placement_hash)
                };

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
                    ctx.options.modular_coordinates ? std::optional<Mat4>(shape_matrix) : std::nullopt,
                    ctx.options.modular_coordinates ? std::optional<f32>(shape_opacity) : std::nullopt,
                    source_is_static
                ));
                // SourceNode ctor sets cache_policy from source_is_static.
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
        // wiring-error log): if any text_run-flagged node lacks an
        // attached shape, surface it ONCE per layer so multi-source
        // errors aren't silent.
        u64 aggregated_params_hash = 0;
        u64 aggregated_source_hash = hash_string(std::string(layer.name) + "_multisource");
        bool saw_orphan_text_run = false;
        for (const auto& node : layer.nodes) {
            if (node.is_text_run_shape && !node.text_run_shape) {
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
                "[source-pass] layer='{}' contains a text_run-flagged node "
                "with null text_run_shape in a multi-node layer — the text "
                "will be skipped at execute time.  Wiring failed to "
                "attach the shape; check LayerBuilder::text_run + "
                "materialize_text_run_shape.",
                layer.name.c_str());
        }

        cache::NodeCacheKey source_key{
            .scope = "layer.multisource:" + std::string(layer.name),
            .frame = source_is_static ? Frame{0} : ctx.frame.frame,
            .width = ctx.frame.width,
            .height = ctx.frame.height,
            .params_hash = aggregated_params_hash,
            .source_hash = aggregated_source_hash
        };

        std::vector<MultiSourceItem> items;
        items.reserve(layer.nodes.size());

        // Items are pushed unconditionally — even text_run-flagged nodes —
        // because MultiSourceNode::execute() dispatches on
        // `item.node->is_text_run_shape` per item.  Order is the
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
            source_is_static
            // MultiSourceNode ctor sets cache_policy from cache_static.
        ));
        return multi_source;
    }

    if (layer.kind == LayerKind::Precomp) {
        const size_t cache_cap   = ctx.options.program_cache_capacity > 0
            ? ctx.options.program_cache_capacity
            : 8;  // default
        const auto tune_mode     = ctx.options.program_cache_tune
            ? cache::TuneMode::Auto
            : cache::TuneMode::Fixed;

        // Create PrecompNode via GraphNodeCatalog to break the
        // graph_builder → graph_pipeline CMake cycle.
        GraphNodeCreateRequest request{
            .payload = PrecompNodeCreateSpec{
                .composition_name =
                    std::string(layer.precomp_composition_name),
                .start_frame = layer.from,
                .duration = layer.duration,
                .cache_frame = is_static ? Frame{0} : Frame{-1},
                .cache_capacity = cache_cap,
                .tune_mode = tune_mode,
                .tune_interval =
                    ctx.options.program_cache_tune_interval,
                .tune_min_capacity =
                    ctx.options.program_cache_tune_min_capacity,
                .tune_max_capacity =
                    ctx.options.program_cache_tune_max_capacity,
            }
        };

        if (!ctx.resources.node_catalog) {
            throw std::logic_error(
                "source.precomp: node_catalog not wired (call wire_precomp_build_factory)");
        }
        auto node = ctx.resources.node_catalog->create(
            "source.precomp", request);

        if (!node) {
            throw std::logic_error(
                "source.precomp factory is not registered");
        }

        auto precomp_id = graph.add_node(std::move(node));
        // PrecompNode ctor uses frame_variant_cache("precomp_animated") by
        // default; PrecompInstanceCreateSpec::cache_frame = 0 is the
        // builder signal for "bake at frame 0" but does not flip the policy.
        // For now we keep the static/animated flag as a stub m_cache_static.
        return precomp_id;
    }

    if (layer.kind == LayerKind::Video && layer.video_source) {
        auto video_id = graph.add_node(std::make_unique<VideoNode>(
            *layer.video_source, ctx.resources.video_decoder, layer.from
        ));
        // VideoNode ctor already sets no_cache("video").
        return video_id;
    }

    return graph.add_node(std::make_unique<ClearNode>());
}

} // namespace chronon3d::graph::detail
