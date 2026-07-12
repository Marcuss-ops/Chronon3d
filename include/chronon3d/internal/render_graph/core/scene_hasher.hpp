#pragma once

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>

namespace chronon3d {
    class Scene;
    struct Camera2_5D;
    using Camera2_5DRuntime = Camera2_5D;
    class Layer;
}

namespace chronon3d::graph {

/**
 * @brief Optimized scene fingerprinting with per-layer incremental hashing.
 * 
 * P4: Instead of hashing the entire scene every frame, we compute 
 * per-layer hashes and combine them. If a layer is static, we reuse its hash.
 *
 * Implementation bodies moved to src/render_graph/core/scene_hasher.cpp
 * to break a circular include dependency (scene.hpp → camera_program.hpp →
 * render_session.hpp → scene_hasher.hpp → scene.hpp blocked by #pragma once,
 * leaving Scene incomplete).  See TICKET-BUILD-ROT-CASCADE-CAMERA surface B.
 */
class SceneHasher {
public:
    uint64_t compute_fingerprint(const Scene& scene, Frame frame);
    uint64_t compute_static_fingerprint(const Scene& scene);
    uint64_t compute_structure_fingerprint(const Scene& scene);

    [[nodiscard]] bool is_static_scene(const Scene& scene) const;
    [[nodiscard]] bool is_static_scene_at(const Scene& scene, Frame frame) const;

    uint64_t compute_active_at_fingerprint(const Scene& scene, Frame frame) const;

    void clear() {
        // No cached state to clear — hashing is stateless.
    }

private:
    static uint64_t hash_layer(const Layer& layer);
    static uint64_t hash_layer_structure(const Layer& layer);

    static bool layer_animation_done_at(const Layer& layer, Frame frame);
    static bool layer_is_static(const Layer& layer);
    static bool layer_is_static_at(const Layer& layer, Frame frame);

    static bool camera_is_static(const Camera2_5DRuntime& cam);

    [[nodiscard]] bool is_effectively_static_at(const Scene& scene, Frame frame) const;
};

} // namespace chronon3d::graph
