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
        
        // We also want to include global scene properties
        h = hash_combine(h, hash_string("scene_root"));
        
        for (const auto& layer : scene.layers()) {
            if (!layer.active_at(frame)) continue;

            const std::string lname(layer.name);
            uint64_t layer_h = 0;
            
            // Check if we can use incremental hashing for this layer
            // For now, we compute the layer hash and combine.
            // A more advanced version would check layer.dirty_flags.
            
            layer_h = hash_combine(layer_h, hash_string(layer.name));
            layer_h = hash_combine(layer_h, static_cast<u64>(layer.kind));
            layer_h = hash_combine(layer_h, layer.visible ? 1 : 0);
            layer_h = hash_combine(layer_h, layer.is_3d ? 1 : 0);
            layer_h = hash_combine(layer_h, layer.cache_static ? 1 : 0);
            layer_h = hash_combine(layer_h, static_cast<u64>(layer.blend_mode));
            layer_h = hash_combine(layer_h, hash_transform(layer.transform));
            layer_h = hash_combine(layer_h, hash_mask(layer.mask));

            for (const auto& node : layer.nodes) {
                layer_h = hash_combine(layer_h, hash_render_node(node));
            }

            if (layer.kind == LayerKind::Precomp) {
                layer_h = hash_combine(layer_h, hash_string(layer.precomp_composition_name));
            }

            // Combine layer hash into scene fingerprint
            h = hash_combine(h, layer_h);
            
            m_layer_hashes[lname] = layer_h;
        }
        
        return h;
    }

    void clear() {
        m_layer_hashes.clear();
    }

private:
    std::unordered_map<std::string, uint64_t> m_layer_hashes;
};

} // namespace chronon3d::graph
