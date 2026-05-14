#include <chronon3d/render_graph/graph_builder.hpp>
#include <chronon3d/render_graph/nodes/basic_nodes.hpp>
#include <chronon3d/render_graph/nodes/precomp_node.hpp>
#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <chronon3d/render_graph/nodes/video_node.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/scene/layer/layer.hpp>
#include <chronon3d/scene/layer/layer_hierarchy.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d::graph {

// ---------------------------------------------------------------------------
// append_layer_pipeline
// Builds: source → transform → [mask] → [effects] → [dof_blur] → composite
// Updates 'current' to the output composite node.
// ---------------------------------------------------------------------------
void GraphBuilder::append_layer_pipeline(
    RenderGraph&          graph,
    const LayerGraphItem& item,
    GraphNodeId&          current,
    const RenderGraphContext& ctx,
    const Camera2_5D&     cam25d
) {
    const Layer& layer = *item.layer;

    GraphNodeId layer_output;

    if (layer.kind == LayerKind::Normal) {
        layer_output = build_layer_source(graph, layer, ctx);
    } else if (layer.kind == LayerKind::Precomp) {
        layer_output = graph.add_node(std::make_unique<PrecompNode>(
            std::string(layer.precomp_composition_name),
            layer.from,
            layer.duration
        ));
    } else {
        // LayerKind::Video
        layer_output = graph.add_node(std::make_unique<VideoNode>(
            layer.video_source,
            ctx.video_decoder,
            layer.from
        ));
    }

    // Transform — use the (possibly projected) transform.
    // Always add a TransformNode for projected layers; for 2D layers only if
    // there is a non-identity transform or the layer type needs it.
    const bool needs_transform = item.projected
        || layer.kind == LayerKind::Precomp
        || layer.kind == LayerKind::Video
        || item.transform.any();

    if (needs_transform) {
        std::unique_ptr<TransformNode> transform_node;
        if (item.projected) {
            // Apply full projection matrix (includes view, proj, and world orientation)
            transform_node = std::make_unique<TransformNode>(item.projection_matrix, layer.transform.opacity);
        } else {
            transform_node = std::make_unique<TransformNode>(item.transform);
        }
        
        auto transform = graph.add_node(std::move(transform_node));
        graph.connect(layer_output, transform);
        layer_output = transform;
    }

    // Mask
    if (layer.mask.enabled()) {
        auto masked = graph.add_node(std::make_unique<MaskNode>(layer.mask));
        graph.connect(layer_output, masked);
        layer_output = masked;
    }

    // Effect stack
    if (!layer.effects.empty()) {
        auto effects = graph.add_node(
            std::make_unique<EffectStackNode>(layer.effects)
        );
        graph.connect(layer_output, effects);
        layer_output = effects;
    }

    // Depth-of-field blur (only for projected 3D layers when DOF is enabled)
    if (item.projected && cam25d.dof.enabled) {
        // Distance from the focus plane in world Z units.
        // item.depth is (world_z - camera_z), so world_z = item.depth + camera_z.
        const f32 world_z = item.depth + cam25d.position.z;
        const f32 dist    = std::abs(world_z - cam25d.dof.focus_z);
        const f32 blur    = std::min(dist * cam25d.dof.aperture, cam25d.dof.max_blur);

        if (blur > 0.5f) {
            EffectStack dof_stack;
            dof_stack.push_back(EffectInstance{BlurParams{blur}});
            auto dof_node = graph.add_node(
                std::make_unique<EffectStackNode>(std::move(dof_stack))
            );
            graph.connect(layer_output, dof_node);
            layer_output = dof_node;
        }
    }

    // Composite onto the current accumulation
    auto composite = graph.add_node(
        std::make_unique<CompositeNode>(layer.blend_mode)
    );
    graph.connect(current, composite);
    graph.connect(layer_output, composite);
    current = composite;
}

// ---------------------------------------------------------------------------
// build
// ---------------------------------------------------------------------------
RenderGraph GraphBuilder::build(const Scene& scene, const RenderGraphContext& ctx) {
    RenderGraph graph;

    GraphNodeId current = graph.add_node(std::make_unique<ClearNode>());

    // ── Root scene nodes ──
    for (const auto& node : scene.nodes()) {
        cache::NodeCacheKey source_key{
            .scope = "root.source:" + std::string(node.name),
            .frame = ctx.frame,
            .width = ctx.width,
            .height = ctx.height,
            .params_hash = hash_render_node(node),
            .source_hash = hash_bytes(node.name.data(), node.name.size())
        };

        auto source = graph.add_node(std::make_unique<SourceNode>(
            std::string(node.name), node, source_key
        ));

        auto composite = graph.add_node(std::make_unique<CompositeNode>(BlendMode::Normal));
        graph.connect(current, composite);
        graph.connect(source, composite);
        current = composite;
    }

    const Camera2_5D& input_cam = scene.camera_2_5d();
    const bool use_25d = input_cam.enabled;

    // ── Resolve Layer Hierarchy ──
    ResolvedCamera resolved_cam;
    const auto resolved_layers = resolve_layer_hierarchy(
        scene.layers(),
        ctx.frame,
        scene.resource(),
        &input_cam,
        &resolved_cam
    );

    const Camera2_5D& cam25d = resolved_cam.camera;

    // ── Helper to append a single item pipeline ──
    auto append_item = [&](const LayerGraphItem& item) {
        append_layer_pipeline(graph, item, current, ctx, cam25d);
    };

    // ── 3D Grouping Logic ──
    // Contiguous 3D layers are grouped into "bins" and sorted by depth internally.
    // 2D layers (and Adjustment layers) break these bins to preserve vertical order.
    std::vector<LayerGraphItem> current_3d_bin;

    auto flush_3d_bin = [&]() {
        if (current_3d_bin.empty()) return;

        // Sort: farthest layers first (higher depth value).
        std::stable_sort(current_3d_bin.begin(), current_3d_bin.end(),
            [](const LayerGraphItem& a, const LayerGraphItem& b) {
                return a.depth > b.depth;
            });

        for (const auto& item : current_3d_bin) {
            append_item(item);
        }
        current_3d_bin.clear();
    };

    for (const auto& resolved : resolved_layers) {
        const Layer& layer = *resolved.layer;

        if (!layer.active_at(ctx.frame)) {
            continue;
        }

        // 1. Adjustment layers: act as barriers and apply effects inline.
        if (layer.kind == LayerKind::Adjustment) {
            flush_3d_bin();
            if (!layer.effects.empty()) {
                auto adj = graph.add_node(std::make_unique<AdjustmentNode>(layer.effects));
                graph.connect(current, adj);
                current = adj;
            }
            continue;
        }

        // 2. Null layers: skip, but they also break 3D bins (to be safe/standard).
        if (layer.kind == LayerKind::Null) {
            flush_3d_bin();
            continue;
        }

        // 3. 3D Layers
        if (use_25d && layer.is_3d) {
            Transform effective_transform = resolved.world_transform;

            // Project through camera.
            auto proj = project_layer_2_5d(
                effective_transform,
                cam25d,
                static_cast<f32>(ctx.width),
                static_cast<f32>(ctx.height)
            );

            if (proj.visible) {
                current_3d_bin.push_back({
                    .layer             = &layer,
                    .transform         = proj.transform,
                    .projection_matrix = proj.projection_matrix,
                    .depth             = proj.depth,
                    .projected         = true,
                    .insertion_index   = resolved.insertion_index
                });
            }
        } 
        // 4. 2D Layers
        else {
            flush_3d_bin();
            append_item({
                .layer           = &layer,
                .transform       = resolved.world_transform,
                .depth           = 0.0f,
                .projected       = false,
                .insertion_index = resolved.insertion_index
            });
        }
    }

    flush_3d_bin();

    graph.set_output(current);
    return graph;
}

// ---------------------------------------------------------------------------
// build_layer_source
// ---------------------------------------------------------------------------
GraphNodeId GraphBuilder::build_layer_source(
    RenderGraph&          graph,
    const Layer&          layer,
    const RenderGraphContext& ctx
) {
    GraphNodeId layer_current = graph.add_node(std::make_unique<ClearNode>());

    for (const auto& node : layer.nodes) {
        cache::NodeCacheKey source_key{
            .scope = "layer.source:" + std::string(layer.name) + ":" + std::string(node.name),
            .frame = ctx.frame,
            .width = ctx.width,
            .height = ctx.height,
            .params_hash = hash_render_node(node),
            .source_hash = hash_bytes(node.name.data(), node.name.size())
        };

        auto source = graph.add_node(std::make_unique<SourceNode>(
            std::string(node.name), node, source_key
        ));

        auto composite = graph.add_node(std::make_unique<CompositeNode>(BlendMode::Normal));
        graph.connect(layer_current, composite);
        graph.connect(source, composite);
        layer_current = composite;
    }

    return layer_current;
}

} // namespace chronon3d::graph
