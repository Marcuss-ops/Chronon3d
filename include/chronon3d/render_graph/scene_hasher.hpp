#pragma once

#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <chronon3d/scene/scene.hpp>
#include <unordered_map>
#include <string>

namespace chronon3d::graph {

/**
 * @brief Optimized scene fingerprinting with per-layer incremental hashing.
 * 
 * P4: Instead of hashing the entire scene every frame, we compute 
 * per-layer hashes and combine them. If a layer is static, we reuse its hash.
 */
class SceneHasher {
public:
    uint64_t compute_fingerprint(const Scene& scene, Frame frame) {
        uint64_t h = 0;
        
        h = hash_combine(h, hash_string("scene_root"));
        
        for (const auto& layer : scene.layers()) {
            if (!layer.active_at(frame)) continue;
            const auto layer_h = hash_layer(layer);
            h = hash_combine(h, layer_h);
        }
        
        for (const auto& node : scene.nodes()) {
            h = hash_combine(h, hash_render_node(node));
        }
        
        return h;
    }

    /// Frame-independent fingerprint: hashes every layer in the scene
    /// regardless of active_at(frame). Two scenes with the same layers
    /// produce the same fingerprint at any frame — enabling the executor
    /// fast-path to trigger for static compositions.
    uint64_t compute_static_fingerprint(const Scene& scene) {
        uint64_t h = 0;
        h = hash_combine(h, hash_string("scene_root"));

        for (const auto& layer : scene.layers()) {
            const auto layer_h = hash_layer(layer);
            h = hash_combine(h, layer_h);
        }

        for (const auto& node : scene.nodes()) {
            h = hash_combine(h, hash_render_node(node));
        }

        return h;
    }

    void clear() {
        m_layer_hashes.clear();
    }

private:
    static uint64_t hash_layer(const Layer& layer) {
        uint64_t h = 0;
        h = hash_combine(h, hash_string(layer.name));
        h = hash_combine(h, static_cast<u64>(layer.kind));
        h = hash_combine(h, layer.visible ? 1 : 0);
        h = hash_combine(h, layer.is_3d ? 1 : 0);
        h = hash_combine(h, layer.cache_static ? 1 : 0);
        h = hash_combine(h, static_cast<u64>(layer.blend_mode));
        h = hash_combine(h, hash_transform(layer.transform));
        h = hash_combine(h, hash_mask(layer.mask));

        for (const auto& node : layer.nodes) {
            h = hash_combine(h, hash_render_node(node));
        }

        if (layer.kind == LayerKind::Precomp) {
            h = hash_combine(h, hash_string(layer.precomp_composition_name));
        }
        return h;
    }

    std::unordered_map<std::string, uint64_t> m_layer_hashes;
};

} // namespace chronon3d::graph
