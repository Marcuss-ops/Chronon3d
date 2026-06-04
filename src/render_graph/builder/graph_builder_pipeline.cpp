#include "graph_builder_pipeline.hpp"

#include "graph_builder_internal.hpp"
#include "passes/graph_builder_source_pass.hpp"
#include "passes/graph_builder_layer_passes.hpp"
#include "passes/graph_builder_lighting_passes.hpp"
#include <chronon3d/render_graph/builder/graph_build_pipeline.hpp>

#include <algorithm>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/render_graph/nodes/track_matte_node.hpp>
#include <chronon3d/render_graph/nodes/transition_node.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <limits>
#include <chronon3d/render_graph/nodes/basic_nodes_common.hpp>
#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <chronon3d/render_graph/nodes/effect_stack_node.hpp>
#include <chronon3d/render_graph/nodes/adjustment_node.hpp>
#include <chronon3d/render_graph/nodes/multi_source_node.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/scene/model/layer.hpp>
#include <chronon3d/scene/model/shape.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include "graph_builder_coordinates.hpp"
#include <chronon3d/core/telemetry/render_telemetry.hpp>

namespace chronon3d::graph::detail {

using namespace chronon3d::graph;

// Phase 4: functions moved from anonymous namespace to detail so pass classes can call them.
// check_static_recursive_impl remains file-local (static).


GraphNodeId append_root_sources(RenderGraph& graph, const Scene& scene,
                                RenderGraphContext& ctx,
                                GraphNodeId current) {
    bool first_root_source = true;
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
            std::string(node.name), node, source_key,
            ctx.modular_coordinates
        ));

        if (first_root_source) {
            if (const auto* source_node = dynamic_cast<const SourceNode*>(&graph.node(source));
                source_node && source_node->can_seed_full_frame(ctx)) {
                ctx.skip_initial_clear = true;
            }
            first_root_source = false;
        }

        auto composite = graph.add_node(std::make_unique<CompositeNode>(chronon3d::BlendMode::Normal));
        graph.connect(current, composite);
        graph.connect(source, composite);
        current = composite;
    }

    return current;
}

GraphNodeId build_matte_sub_pipeline(
    RenderGraph& graph, const LayerGraphItem& item, const RenderGraphContext& ctx)
{
    std::string prev_layer = g_current_builder_layer_id;
    g_current_builder_layer_id = std::string(item.layer->name);
    GraphNodeId out = append_source_pass(graph, item, ctx);
    if (out == k_invalid_node) {
        g_current_builder_layer_id = prev_layer;
        return k_invalid_node;
    }
    append_transform_pass_if_needed(graph, out, item, ctx);
    g_current_builder_layer_id = prev_layer;
    return out;
}

void append_layer_pipeline(RenderGraph& graph, const LayerGraphItem& item,
                           GraphNodeId& current, RenderGraphContext& ctx,
                           const Camera2_5DRuntime& cam25d,
                           std::span<const ShadowCasterInfo> casters,
                           const rendering::DepthGrade& depth_grade) {
    std::string prev_layer = g_current_builder_layer_id;
    g_current_builder_layer_id = std::string(item.layer->name);

    GraphNodeId layer_output = append_source_pass(graph, item, ctx);
    const Layer& layer = *item.layer;

    if (!ctx.skip_initial_clear && layer_output != k_invalid_node) {
        const bool simple_opaque_full_frame_layer =
            layer.kind == LayerKind::Normal &&
            layer.nodes.size() == 1 &&
            layer.mask.type == MaskType::None &&
            layer.effects.empty() &&
            layer.blend_mode == BlendMode::Normal &&
            !layer.track_matte.active() &&
            (layer.transition_in.transition_id.empty() || layer.transition_in.transition_id == "none") &&
            (layer.transition_out.transition_id.empty() || layer.transition_out.transition_id == "none") &&
            !layer.nodes[0].shadow.enabled &&
            !layer.nodes[0].glow.enabled;

        const auto* source_node = dynamic_cast<const SourceNode*>(&graph.node(layer_output));
        const bool can_seed = source_node && source_node->can_seed_full_frame(ctx);

        if (simple_opaque_full_frame_layer) {
            if (can_seed) {
                ctx.skip_initial_clear = true;
            }
        }
    }

    if (layer.kind == LayerKind::Adjustment) {
        for (const auto& eff : layer.effects) {
            chronon3d::EffectStack stack;
            stack.push_back(eff);
            auto node = std::make_unique<AdjustmentNode>(std::move(stack));
            GraphNodeId adj_id = graph.add_node(std::move(node));
            graph.node(adj_id).set_frame_dependent(!(layer.cache_static || item.is_static));
            graph.connect(current, adj_id);
            current = adj_id;
        }
        g_current_builder_layer_id = prev_layer;
        return;
    }

    const bool mask_before_transform =
        layer.kind == LayerKind::Normal ||
        layer.kind == LayerKind::Precomp ||
        layer.kind == LayerKind::Video;

    if (mask_before_transform)
        append_mask_pass_if_needed(graph, layer_output, item, ctx);

    append_transform_pass_if_needed(graph, layer_output, item, ctx);

    if (!mask_before_transform)
        append_mask_pass_if_needed(graph, layer_output, item, ctx);

    append_lighting_pass_if_needed(graph, layer_output, item, ctx);
    append_shadow_passes_if_needed(graph, layer_output, item, casters, ctx);

    // Depth grading: per-layer fog / desaturation / contrast based on world Z
    append_depth_grade_pass_if_needed(graph, layer_output, item, ctx, depth_grade);

    append_effect_pass_if_needed(graph, layer_output, *item.layer, item, cam25d, ctx);

    if (layer.track_matte.active() && item.matte_node != k_invalid_node) {
        cache::NodeCacheKey matte_key{
            .scope       = "matte:" + std::string(layer.name),
            .frame       = (layer.cache_static || item.is_static) ? Frame{0} : ctx.frame,
            .width       = ctx.width,
            .height      = ctx.height,
            .params_hash = hash_bytes(layer.track_matte.source_layer.data(),
                                      layer.track_matte.source_layer.size()),
        };
        matte_key.params_hash = hash_combine(
            matte_key.params_hash,
            static_cast<u64>(layer.track_matte.type));

        auto matte_node = graph.add_node(
            std::make_unique<TrackMatteNode>(layer.track_matte.type,
                                              std::string(layer.name), matte_key));
        graph.node(matte_node).set_frame_dependent(!(layer.cache_static || item.is_static));
        graph.connect(layer_output,     matte_node);
        graph.connect(item.matte_node,  matte_node);
        layer_output = matte_node;
    }

    const bool has_in_trans = !layer.transition_in.transition_id.empty() && layer.transition_in.transition_id != "none";
    const bool has_out_trans = !layer.transition_out.transition_id.empty() && layer.transition_out.transition_id != "none";

    if (has_in_trans || has_out_trans) {
        std::string trans_id = "none";
        LayerTransitionSpec active_spec;
        bool is_out = false;

        // TransitionNode now computes progress internally from ctx.time_seconds
        if (has_in_trans) {
            trans_id = layer.transition_in.transition_id;
            active_spec = layer.transition_in;
            is_out = false;
        } else if (has_out_trans) {
            trans_id = layer.transition_out.transition_id;
            active_spec = layer.transition_out;
            is_out = true;
        }

        if (trans_id != "none") {
            auto trans_node = graph.add_node(std::make_unique<TransitionNode>(
                std::string(layer.name), active_spec, is_out, layer.from, layer.duration
            ));
            graph.node(trans_node).set_frame_dependent(true);
            graph.connect(layer_output, trans_node);
            layer_output = trans_node;
        }
    }

    append_composite_pass(graph, current, layer_output, *item.layer, (layer.cache_static || item.is_static), ctx, item.world_z);
    g_current_builder_layer_id = prev_layer;
}

void sort_camera25d_layers(std::vector<LayerGraphItem>& items) {
    std::stable_sort(items.begin(), items.end(),
        [](const LayerGraphItem& a, const LayerGraphItem& b) {
            if (a.depth != b.depth) return a.depth > b.depth;
            return a.insertion_index < b.insertion_index;
        });
}

// File-local helper for compute_static_layers.
static bool check_static_recursive_impl(
    const ResolvedLayer& rl,
    const std::unordered_map<std::string, const ResolvedLayer*>& name_to_resolved,
    std::unordered_map<std::string, bool>& is_static_cache,
    std::unordered_set<std::string>& visited)
{
    const Layer& l = *rl.layer;
    std::string name(l.name);

    auto cached_it = is_static_cache.find(name);
    if (cached_it != is_static_cache.end()) {
        return cached_it->second;
    }

    if (visited.count(name)) {
        return false;
    }
    visited.insert(name);

    bool static_flag = false;
    if (l.cache_static) {
        static_flag = true;
    } else {
        bool is_local_static = true;
        if (l.kind == LayerKind::Video || l.kind == LayerKind::Precomp) {
            is_local_static = false;
        } else if ((!l.transition_in.transition_id.empty() && l.transition_in.transition_id != "none") ||
                   (!l.transition_out.transition_id.empty() && l.transition_out.transition_id != "none")) {
            is_local_static = false;
        } else if (l.anim_transform.is_animated()) {
            is_local_static = false;
        } else if (l.anim_transform.position.has_expression() ||
                   l.anim_transform.rotation_euler.has_expression() ||
                   l.anim_transform.scale.has_expression() ||
                   l.anim_transform.anchor.has_expression() ||
                   l.anim_transform.opacity.has_expression()) {
            is_local_static = false;
        }

        if (is_local_static) {
            if (rl.has_parent && !rl.parent_missing && !rl.cycle_detected) {
                auto parent_it = name_to_resolved.find(std::string(l.parent_name));
                if (parent_it != name_to_resolved.end()) {
                    if (!check_static_recursive_impl(*parent_it->second, name_to_resolved, is_static_cache, visited)) {
                        is_local_static = false;
                    }
                }
            }
        }

        if (is_local_static) {
            if (l.track_matte.active()) {
                auto matte_it = name_to_resolved.find(std::string(l.track_matte.source_layer));
                if (matte_it != name_to_resolved.end()) {
                    if (!check_static_recursive_impl(*matte_it->second, name_to_resolved, is_static_cache, visited)) {
                        is_local_static = false;
                    }
                }
            }
        }

        static_flag = is_local_static;
    }

    visited.erase(name);
    is_static_cache[name] = static_flag;
    return static_flag;
}

// Phase 4: compute_static_layers overload taking vector directly (for ResolvePass)
void compute_static_layers(
    const std::pmr::vector<ResolvedLayer>& layers,
    std::unordered_map<std::string, bool>& is_static_cache)
{
    std::unordered_map<std::string, const ResolvedLayer*> name_to_resolved;
    for (const auto& rl : layers) {
        name_to_resolved[std::string(rl.layer->name)] = &rl;
    }
    std::unordered_set<std::string> visited;
    for (const auto& rl : layers) {
        check_static_recursive_impl(rl, name_to_resolved, is_static_cache, visited);
    }
}


bool is_native_3d_layer(const Layer& layer) {
    for (const auto& node : layer.nodes) {
        if (node.shape.type == ShapeType::FakeBox3D ||
            node.shape.type == ShapeType::GridPlane) {
            return true;
        }
    }
    return false;
}

raster::BBox compute_layer_bbox(const LayerGraphItem& item, const RenderGraphContext& ctx, SoftwareRenderer* renderer) {
    const Layer& layer = *item.layer;

    if (layer.kind == LayerKind::Adjustment) {
        return raster::BBox{0, 0, ctx.width, ctx.height};
    }

    if (layer.kind != LayerKind::Normal) {
        return raster::BBox{0, 0, ctx.width, ctx.height};
    }

    if (!renderer) {
        return raster::BBox{0, 0, ctx.width, ctx.height};
    }

    const Mat4 ssaa_scale = glm::scale(Mat4(1.0f), Vec3(ctx.ssaa_factor, ctx.ssaa_factor, 1.0f));
    const Mat4 canvas_center = glm::translate(Mat4(1.0f), Vec3(ctx.width * 0.5f, ctx.height * 0.5f, 0.0f));
    const bool centered = should_use_centered_rendering(item, ctx);

    const bool layer_needs_transform = layer_needs_render_transform(item, ctx);
    const bool use_local = ctx.modular_coordinates && layer_needs_transform && !item.native_3d;

    raster::BBox layer_bbox{
        .x0 = std::numeric_limits<i32>::max(),
        .y0 = std::numeric_limits<i32>::max(),
        .x1 = std::numeric_limits<i32>::min(),
        .y1 = std::numeric_limits<i32>::min()
    };

    const Mat4 item_source_world = use_local
        ? item.world_matrix
        : source_space_world_matrix(item, ctx);

    for (const auto& node : layer.nodes) {
        if (!node.visible) continue;

        const Mat4 node_matrix = node.world_transform.to_mat4();
        Mat4 actual_world_matrix;
        if (item.projected) {
            actual_world_matrix = node_matrix;
        } else {
            const Mat4 layer_inv = layer.transform.any() ? glm::inverse(layer.transform.to_mat4()) : Mat4(1.0f);
            actual_world_matrix = layer.hierarchy_resolved
                ? (item_source_world * node_matrix)
                : (item_source_world * layer_inv * node_matrix);
        }

        Mat4 matrix;
        if (use_local) {
            Mat4 shape_matrix = glm::inverse(item.world_matrix) * actual_world_matrix;
            matrix = canvas_center * ssaa_scale * shape_matrix;
        } else {
            if (item.projected || centered) {
                matrix = canvas_center * ssaa_scale * actual_world_matrix;
            } else {
                matrix = ssaa_scale * actual_world_matrix;
            }
        }

        auto* processor = renderer->software_registry().get_shape(node.shape.type);
        if (!processor) {
            return raster::BBox{0, 0, ctx.width, ctx.height};
        }

        f32 spread = 0.0f;
        if (node.shadow.enabled) spread = std::max(spread, node.shadow.radius);
        if (node.glow.enabled)   spread = std::max(spread, node.glow.radius);

        raster::BBox node_bbox = processor->compute_world_bbox(node.shape, matrix, spread);
        if (!node_bbox.is_empty()) {
            layer_bbox.x0 = std::min(layer_bbox.x0, node_bbox.x0);
            layer_bbox.y0 = std::min(layer_bbox.y0, node_bbox.y0);
            layer_bbox.x1 = std::max(layer_bbox.x1, node_bbox.x1);
            layer_bbox.y1 = std::max(layer_bbox.y1, node_bbox.y1);
        }
    }

    if (layer_bbox.x0 > layer_bbox.x1 || layer_bbox.y0 > layer_bbox.y1) {
        return raster::BBox{0, 0, ctx.width, ctx.height};
    }

    if (item.projected) {
        const Mat4 model = item.projection_matrix;
        const Mat4 dst_canvas_offset = glm::translate(Mat4(1.0f), Vec3(ctx.width * 0.5f, ctx.height * 0.5f, 0.0f));
        const Mat4 src_canvas_offset = glm::translate(Mat4(1.0f), Vec3(ctx.width * 0.5f, ctx.height * 0.5f, 0.0f));
        const Mat4 pixel_model = dst_canvas_offset * model * glm::inverse(src_canvas_offset);

        Vec4 corners[4] = {
            pixel_model * Vec4(static_cast<f32>(layer_bbox.x0), static_cast<f32>(layer_bbox.y0), 0.0f, 1.0f),
            pixel_model * Vec4(static_cast<f32>(layer_bbox.x1), static_cast<f32>(layer_bbox.y0), 0.0f, 1.0f),
            pixel_model * Vec4(static_cast<f32>(layer_bbox.x1), static_cast<f32>(layer_bbox.y1), 0.0f, 1.0f),
            pixel_model * Vec4(static_cast<f32>(layer_bbox.x0), static_cast<f32>(layer_bbox.y1), 0.0f, 1.0f)
        };

        f32 min_x = 1e10f, max_x = -1e10f;
        f32 min_y = 1e10f, max_y = -1e10f;
        for (auto& c : corners) {
            if (std::abs(c.w) < 1e-6f) continue;
            f32 px = c.x / c.w;
            f32 py = c.y / c.w;
            min_x = std::min(min_x, px);
            max_x = std::max(max_x, px);
            min_y = std::min(min_y, py);
            max_y = std::max(max_y, py);
        }

        if (min_x > max_x || min_y > max_y) {
            return raster::BBox{0, 0, ctx.width, ctx.height};
        }

        layer_bbox.x0 = static_cast<i32>(std::floor(min_x));
        layer_bbox.y0 = static_cast<i32>(std::floor(min_y));
        layer_bbox.x1 = static_cast<i32>(std::ceil(max_x));
        layer_bbox.y1 = static_cast<i32>(std::ceil(max_y));
    } else if (use_local) {
        Mat4 model = item.world_matrix;
        bool centered_render = should_use_centered_rendering(item, ctx);
        if (centered_render) {
            Mat4 ssaa_world = item.world_matrix;
            ssaa_world[3][0] *= ctx.ssaa_factor;
            ssaa_world[3][1] *= ctx.ssaa_factor;
            ssaa_world[3][2] *= ctx.ssaa_factor;
            model =
                glm::translate(Mat4(1.0f), Vec3(-ctx.width * 0.5f, -ctx.height * 0.5f, 0.0f)) *
                ssaa_world;
        }
        const Mat4 dst_canvas_offset = glm::translate(Mat4(1.0f), Vec3(ctx.width * 0.5f, ctx.height * 0.5f, 0.0f));
        const Mat4 src_canvas_offset = glm::translate(Mat4(1.0f), Vec3(ctx.width * 0.5f, ctx.height * 0.5f, 0.0f));
        const Mat4 pixel_model = dst_canvas_offset * model * glm::inverse(src_canvas_offset);

        Vec4 corners[4] = {
            pixel_model * Vec4(static_cast<f32>(layer_bbox.x0), static_cast<f32>(layer_bbox.y0), 0.0f, 1.0f),
            pixel_model * Vec4(static_cast<f32>(layer_bbox.x1), static_cast<f32>(layer_bbox.y0), 0.0f, 1.0f),
            pixel_model * Vec4(static_cast<f32>(layer_bbox.x1), static_cast<f32>(layer_bbox.y1), 0.0f, 1.0f),
            pixel_model * Vec4(static_cast<f32>(layer_bbox.x0), static_cast<f32>(layer_bbox.y1), 0.0f, 1.0f)
        };

        f32 min_x = 1e10f, max_x = -1e10f;
        f32 min_y = 1e10f, max_y = -1e10f;
        for (auto& c : corners) {
            if (std::abs(c.w) < 1e-6f) continue;
            f32 px = c.x / c.w;
            f32 py = c.y / c.w;
            min_x = std::min(min_x, px);
            max_x = std::max(max_x, px);
            min_y = std::min(min_y, py);
            max_y = std::max(max_y, py);
        }

        if (min_x > max_x || min_y > max_y) {
            return raster::BBox{0, 0, ctx.width, ctx.height};
        }

        layer_bbox.x0 = static_cast<i32>(std::floor(min_x));
        layer_bbox.y0 = static_cast<i32>(std::floor(min_y));
        layer_bbox.x1 = static_cast<i32>(std::ceil(max_x));
        layer_bbox.y1 = static_cast<i32>(std::ceil(max_y));
    }

    return layer_bbox;
}

void compute_static_layers(
    const LayerResolutionResult& resolved,
    std::unordered_map<std::string, bool>& is_static_cache)
{
    std::unordered_map<std::string, const ResolvedLayer*> name_to_resolved;
    for (const auto& rl : resolved.layers) {
        name_to_resolved[std::string(rl.layer->name)] = &rl;
    }

    std::unordered_set<std::string> visited;
    for (const auto& rl : resolved.layers) {
        check_static_recursive_impl(rl, name_to_resolved, is_static_cache, visited);
    }
}

LayerGraphItem make_item_for_matte_source(
    const ResolvedLayer& rl,
    const RenderGraphContext& ctx,
    const Camera2_5DRuntime& cam25d,
    const std::unordered_map<std::string, bool>& is_static_cache)
{
    const bool is_static_val = is_static_cache.at(std::string(rl.layer->name));
    if (cam25d.enabled && rl.layer->uses_2_5d_projection) {
        Transform effective_transform = rl.world_transform;
        const Mat4 projection_world_matrix = effective_transform.to_mat4();
        auto proj = project_layer_2_5d(
            effective_transform,
            projection_world_matrix,
            cam25d,
            static_cast<f32>(ctx.width),
            static_cast<f32>(ctx.height),
            ctx.diagnostics_enabled
        );
        if (proj.visible) {
            const Mat4 eff_proj = is_native_3d_layer(*rl.layer)
                ? Mat4(1.0f)
                : proj.projection_matrix;
            return LayerGraphItem{
                .layer             = rl.layer,
                .transform         = proj.transform,
                .world_matrix      = rl.world_matrix,
                .projection_matrix = eff_proj,
                .depth             = proj.depth,
                .world_z           = rl.world_transform.position.z,
                .projected         = true,
                .native_3d         = is_native_3d_layer(*rl.layer),
                .insertion_index   = rl.insertion_index,
                .matte_node        = k_invalid_node,
                .is_static         = is_static_val,
            };
        }
    }
    return LayerGraphItem{
        .layer           = rl.layer,
        .transform       = rl.world_transform,
        .world_matrix    = rl.world_matrix,
        .depth           = 0.0f,
        .world_z         = rl.world_transform.position.z,
        .projected       = false,
        .native_3d       = is_native_3d_layer(*rl.layer),
        .insertion_index = rl.insertion_index,
        .matte_node      = k_invalid_node,
        .is_static       = is_static_val,
    };
}


} // namespace chronon3d::graph::detail

namespace chronon3d::graph {

RenderGraph GraphBuilder::build(const Scene& scene, const RenderGraphContext& ctx) {
    CHRONON_ZONE_C("build_render_graph", trace_category::kGraph);
    auto mutable_ctx = ctx;

    // Delegate to GraphBuildPipeline which reads passes from GraphBuildRegistry.
    GraphBuildPipeline pipeline;
    pipeline.add_default_passes();
    return pipeline.build(scene, mutable_ctx);
}

} // namespace chronon3d::graph
