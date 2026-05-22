#include "graph_builder_static.hpp"
#include "graph_builder_bbox.hpp"
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <unordered_set>

namespace chronon3d::graph::detail {

namespace {

bool check_static_recursive_impl(
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
        return false; // Loop fallback
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

} // namespace

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
    if (cam25d.enabled && rl.layer->is_3d) {
        Transform effective_transform = rl.world_transform;
        if (!ctx.modular_coordinates) {
            effective_transform.position.x -= ctx.width * 0.5f;
            effective_transform.position.y -= ctx.height * 0.5f;
        }
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
