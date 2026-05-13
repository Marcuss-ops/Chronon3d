#include "render_graph_builder.hpp"
#include "render_hash_utils.hpp"
#include <chronon3d/scene/layer_hierarchy.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <algorithm>

namespace chronon3d {
namespace renderer {

namespace {

using rendergraph::RenderCacheKey;
using rendergraph::hash_combine;
using rendergraph::NodeId;

template <typename T>
[[nodiscard]] u64 hash_value_local(const T& value) {
    return rendergraph::hash_bytes(&value, sizeof(T));
}

[[nodiscard]] RenderCacheKey make_key(std::string scope, Frame frame, i32 width, i32 height,
                                     u64 params_hash, u64 source_hash = 0, u64 input_hash = 0) {
    return RenderCacheKey{
        .scope = std::move(scope),
        .frame = frame,
        .width = width,
        .height = height,
        .params_hash = params_hash,
        .source_hash = source_hash,
        .input_hash = input_hash,
    };
}

} // namespace

rendergraph::RenderGraph build_software_render_graph(
    const SoftwareRenderer& renderer,
    const Scene& scene,
    const Camera& camera,
    i32 width,
    i32 height,
    Frame frame,
    f32 frame_time)
{
    using namespace rendergraph;

    RenderGraph graph;
    const u64 frame_time_hash = hash_value_local(frame_time);
    u64 scene_hash = hash_value_local(scene.nodes().size());
    for (const auto& node : scene.nodes()) {
        scene_hash = hash_combine(scene_hash, hash_node(node));
    }
    for (const auto& layer : scene.layers()) {
        scene_hash = hash_combine(scene_hash, hash_layer(layer));
    }
    scene_hash = hash_combine(scene_hash, hash_camera(camera));
    scene_hash = hash_combine(scene_hash, hash_camera_2_5d(scene.camera_2_5d()));

    RenderCacheKey canvas_key = make_key("canvas", frame, width, height,
                                         hash_combine(scene_hash, frame_time_hash));
    NodeId current = graph.add_output("canvas", canvas_key);
    u64 current_hash = canvas_key.digest();

    auto append_root_source = [&](const ::chronon3d::RenderNode& node, NodeId input) {
        const u64 node_hash = hash_node(node);
        const auto transform_key = make_key("root.transform", frame, width, height,
                                            hash_combine(hash_transform(node.world_transform),
                                                         hash_combine(scene_hash, frame_time_hash)),
                                            node_hash, current_hash);
        NodeId transformed = graph.add_transform(std::string(node.name) + ".transform",
                                                 transform_key, input, node.world_transform,
                                                 RenderState{Mat4(1.0f), 1.0f});
        current_hash = transform_key.digest();

        const auto source_key = make_key("root.source", frame, width, height,
                                         hash_combine(node_hash, hash_combine(scene_hash, frame_time_hash)),
                                         node_hash, current_hash);
        NodeId source = graph.add_source(std::string(node.name) + ".source",
                                         source_key, transformed, node);
        current_hash = source_key.digest();

        const auto composite_key = make_key("root.composite", frame, width, height,
                                            hash_combine(static_cast<u64>(BlendMode::Normal),
                                                         hash_combine(scene_hash, frame_time_hash)),
                                            node_hash, current_hash);
        current = graph.add_composite(std::string(node.name) + ".composite",
                                      composite_key, current, source, BlendMode::Normal);
        current_hash = composite_key.digest();
    };

    auto append_layer_pipeline = [&](const Layer& layer, const Transform& transform,
                                     NodeId input, const std::string& scope_prefix,
                                     std::optional<f32> dof_blur = std::nullopt) {
        const u64 layer_hash = hash_layer(layer);
        const auto transform_key = make_key(scope_prefix + ".transform", frame, width, height,
                                            hash_combine(hash_transform(transform),
                                                         hash_combine(scene_hash, frame_time_hash)),
                                            layer_hash, current_hash);
        NodeId transformed = graph.add_transform(scope_prefix + ".transform",
                                                 transform_key, input, transform,
                                                 RenderState{Mat4(1.0f), 1.0f});
        current_hash = transform_key.digest();

        const auto source_key = make_key(scope_prefix + ".source", frame, width, height,
                                         hash_combine(layer_hash, hash_combine(scene_hash, frame_time_hash)),
                                         layer_hash, current_hash);
        NodeId source = graph.add_layer_source(scope_prefix + ".source",
                                               source_key, transformed, layer);
        current_hash = source_key.digest();

        NodeId after_effects = source;
        if (!layer.effects.empty()) {
            const auto effect_key = make_key(scope_prefix + ".effect", frame, width, height,
                                             hash_combine(hash_effect_stack(layer.effects),
                                                          hash_combine(scene_hash, frame_time_hash)),
                                             layer_hash, current_hash);
            after_effects = graph.add_effect(scope_prefix + ".effect",
                                             effect_key, source, layer);
            current_hash = effect_key.digest();
        }

        if (dof_blur.has_value() && *dof_blur > 0.5f) {
            Layer blur_layer = layer;
            blur_layer.effects.clear();
            blur_layer.effects.push_back(EffectInstance{BlurParams{*dof_blur}});

            const auto blur_key = make_key(scope_prefix + ".dof", frame, width, height,
                                           hash_combine(hash_value_local(*dof_blur), hash_combine(scene_hash, frame_time_hash)),
                                           layer_hash, current_hash);
            after_effects = graph.add_effect(scope_prefix + ".dof",
                                             blur_key, after_effects, blur_layer);
            current_hash = blur_key.digest();
        }

        const auto composite_key = make_key(scope_prefix + ".composite", frame, width, height,
                                            hash_combine(static_cast<u64>(layer.blend_mode),
                                                         hash_combine(scene_hash, frame_time_hash)),
                                            layer_hash, current_hash);
        current = graph.add_composite(scope_prefix + ".composite",
                                      composite_key, current, after_effects, layer.blend_mode);
        current_hash = composite_key.digest();
    };

    auto apply_adjustment_layer = [&](const Layer& layer, const std::string& scope_prefix) {
        if (!layer.effects.empty()) {
            const u64 layer_hash = hash_layer(layer);
            const auto effect_key = make_key(scope_prefix + ".adjustment", frame, width, height,
                                             hash_combine(hash_effect_stack(layer.effects),
                                                          hash_combine(scene_hash, frame_time_hash)),
                                             layer_hash, current_hash);
            current = graph.add_effect(scope_prefix + ".adjustment",
                                       effect_key, current, layer);
            current_hash = effect_key.digest();
        }
    };

    for (const auto& node : scene.nodes()) {
        if (!node.visible) continue;
        append_root_source(node, current);
    }

    ResolvedCamera resolved_cam;
    const auto resolved_layers = resolve_layer_hierarchy(
        scene.layers(), frame, scene.resource(), &scene.camera_2_5d(), &resolved_cam);
    const Camera2_5D& cam25 = resolved_cam.camera;

    struct LayerRenderItem {
        Layer layer;
        Transform projected_transform{};
        f32 depth{0.0f};
        usize insertion_index{0};
    };

    std::vector<LayerRenderItem> three_d_layers;
    three_d_layers.reserve(resolved_layers.size());

    for (const auto& resolved : resolved_layers) {
        const Layer& layer = *resolved.layer;

        if (!layer.visible || !layer.active_at(frame)) {
            continue;
        }

        if (layer.kind == LayerKind::Adjustment) {
            apply_adjustment_layer(layer, std::string(layer.name));
            continue;
        }

        if (layer.kind == LayerKind::Glass) {
            const u64 layer_hash = hash_layer(layer);
            // Apply layer transform so glass panel is positioned correctly in screen space
            const auto xform_key = make_key(std::string(layer.name) + ".glass.transform", frame, width, height,
                                            hash_combine(hash_transform(resolved.world_transform),
                                                         hash_combine(scene_hash, frame_time_hash)),
                                            layer_hash, current_hash);
            NodeId xformed = graph.add_transform(std::string(layer.name) + ".glass.transform",
                                                 xform_key, current, resolved.world_transform,
                                                 RenderState{Mat4(1.0f), 1.0f});
            current_hash = xform_key.digest();

            const auto glass_key = make_key(std::string(layer.name) + ".glass", frame, width, height,
                                            hash_combine(layer_hash, hash_combine(scene_hash, frame_time_hash)),
                                            layer_hash, current_hash);
            current = graph.add_glass(std::string(layer.name) + ".glass",
                                      glass_key, xformed, layer);
            current_hash = glass_key.digest();
            continue;
        }

        if (layer.kind == LayerKind::Null) {

            continue;
        }

        if (!cam25.enabled || !layer.is_3d) {
            append_layer_pipeline(layer, resolved.world_transform, current, std::string(layer.name));
        } else {
            auto projected = project_layer_2_5d(
                resolved.world_transform, cam25, static_cast<f32>(width), static_cast<f32>(height));
            if (projected.visible) {
                three_d_layers.push_back({layer, projected.transform, projected.depth, resolved.insertion_index});
            }
        }
    }

    if (cam25.enabled && !three_d_layers.empty()) {
        const Mat4  fake3d_view  = get_camera_view_matrix(cam25);
        const f32   fake3d_focal = (cam25.projection_mode == Camera2_5DProjectionMode::Fov)
            ? focal_length_from_fov(static_cast<f32>(height), cam25.fov_deg)
            : cam25.zoom;
        const f32 vp_cx = static_cast<f32>(width)  * 0.5f;
        const f32 vp_cy = static_cast<f32>(height) * 0.5f;

        for (auto& item : three_d_layers) {
            for (auto& nd : item.layer.nodes) {
                if (nd.shape.type == ShapeType::FakeBox3D) {
                    nd.shape.fake_box3d.cam_ready = true;
                    nd.shape.fake_box3d.cam_view  = fake3d_view;
                    nd.shape.fake_box3d.cam_focal  = fake3d_focal;
                    nd.shape.fake_box3d.vp_cx      = vp_cx;
                    nd.shape.fake_box3d.vp_cy      = vp_cy;
                } else if (nd.shape.type == ShapeType::GridPlane) {
                    nd.shape.grid_plane.cam_ready = true;
                    nd.shape.grid_plane.cam_view  = fake3d_view;
                    nd.shape.grid_plane.cam_focal  = fake3d_focal;
                    nd.shape.grid_plane.vp_cx      = vp_cx;
                    nd.shape.grid_plane.vp_cy      = vp_cy;
                }
            }
        }
    }

    if (cam25.enabled) {
        std::stable_sort(three_d_layers.begin(), three_d_layers.end(),
            [](const LayerRenderItem& a, const LayerRenderItem& b) {
                return a.depth > b.depth;
            });

        for (const auto& item : three_d_layers) {
            const Layer& layer = item.layer;
            std::optional<f32> dof_blur;
            if (cam25.dof.enabled) {
                const f32 dist = std::abs(layer.transform.position.z - cam25.dof.focus_z);
                const f32 blur = std::min(dist * cam25.dof.aperture, cam25.dof.max_blur);
                if (blur > 0.5f) {
                    dof_blur = blur;
                }
            }
            append_layer_pipeline(layer, item.projected_transform, current, std::string(layer.name), dof_blur);
        }
    }

    return graph;
}

} // namespace renderer
} // namespace chronon3d
