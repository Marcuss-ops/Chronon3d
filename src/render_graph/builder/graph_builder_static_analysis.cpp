#include "graph_builder_pipeline.hpp"

#include <chronon3d/scene/model/layer/layer.hpp>

#include <string>
#include <unordered_map>
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
        } else if (l.anim_transform.is_time_dependent()) {
            is_local_static = false;
        }

        if (is_local_static && rl.has_parent && !rl.parent_missing && !rl.cycle_detected) {
            auto parent_it = name_to_resolved.find(std::string(l.parent_name));
            if (parent_it != name_to_resolved.end()) {
                if (!check_static_recursive_impl(*parent_it->second, name_to_resolved, is_static_cache, visited)) {
                    is_local_static = false;
                }
            }
        }

        if (is_local_static && l.track_matte.active()) {
            auto matte_it = name_to_resolved.find(std::string(l.track_matte.source_layer));
            if (matte_it != name_to_resolved.end()) {
                if (!check_static_recursive_impl(*matte_it->second, name_to_resolved, is_static_cache, visited)) {
                    is_local_static = false;
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

bool is_native_3d_layer(const Layer& layer) {
    for (const auto& node : layer.nodes) {
        if (node.shape.type == ShapeType::FakeBox3D ||
            node.shape.type == ShapeType::GridPlane) {
            return true;
        }
    }
    return false;
}

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

void compute_static_layers(
    const LayerResolutionResult& resolved,
    std::unordered_map<std::string, bool>& is_static_cache)
{
    compute_static_layers(resolved.layers, is_static_cache);
}

} // namespace chronon3d::graph::detail
