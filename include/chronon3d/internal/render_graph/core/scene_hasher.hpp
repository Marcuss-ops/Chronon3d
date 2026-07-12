#pragma once

#include <chronon3d/scene/model/layer/layer.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <string>

namespace chronon3d {
class Scene;
}

namespace chronon3d::graph {

/**
 * @brief Optimized scene fingerprinting with per-layer incremental hashing.
 * 
 * P4: Instead of hashing the entire scene every frame, we compute 
 * per-layer hashes and combine them. If a layer is static, we reuse its hash.
 *
 * Implementation bodies moved to src/render_graph/scene_hasher.cpp
 * to break a circular include dependency (scene.hpp -> camera_program.hpp ->
 * render_session.hpp -> scene_hasher.hpp -> scene.hpp blocked by #pragma once,
 * leaving Scene incomplete).  See TICKET-BUILD-ROT-CASCADE-CAMERA surface B.
 */
class SceneHasher {
public:
    uint64_t compute_fingerprint(const Scene& scene, Frame frame);

    /// Frame-independent fingerprint: hashes every layer in the scene
    /// regardless of active_at(frame). Two scenes with the same layers
    /// produce the same fingerprint at any frame — enabling the executor
    /// fast-path to trigger for static compositions.
    uint64_t compute_static_fingerprint(const Scene& scene);

    /// Frame-independent structure fingerprint used to decide whether a cached
    /// compiled graph can be safely reused.  This intentionally ignores the
    /// per-node render payload (for example changing text content) while still
    /// tracking the layer topology and the graph-affecting layer settings.
    uint64_t compute_structure_fingerprint(const Scene& scene);

    /// Returns true if the scene is "static" — i.e. safe to reuse the same
    /// framebuffer across consecutive frames. A scene is static when:
    ///   - No layer has AnimatedTransform with actual keyframes or expressions
    ///   - No layer is a Video layer (video sources are inherently frame-dependent)
    ///   - No layer has time-dependent expressions in its AnimatedValues
    ///   - Camera has no animation
    /// When static, we relax the m_prev_frame == frame check to allow
    /// m_prev_frame == frame - 1 (consecutive frames), enabling the fast-path
    /// to work in real video exports (frames 0,1,2,3...) not just benchmarks.
    [[nodiscard]] bool is_static_scene(const Scene& scene) const;

    /// Frame-aware variant: returns true if the scene is static AT the given
    /// frame (i.e., the scene output won't change between frame and frame+1).
    /// Unlike is_static_scene() which rejects ANY layer with keyframes, this
    /// accounts for animations that have reached their terminal state.
    /// For example, tracking_breathing(1.04f, Frame{120}) on a 150-frame comp:
    /// frames 120-149 are effectively static (scale has settled at 1.0).
    [[nodiscard]] bool is_static_scene_at(const Scene& scene, Frame frame) const;

    /// Frame-dependent fingerprint that captures which layers are active at a
    /// specific frame. Unlike compute_static_fingerprint() (which hashes all
    /// layers unconditionally), this incorporates active_at(frame) — so scenes
    /// where layers activate/deactivate across frames produce different fingerprints.
    /// Used by the static fast-path to ensure we only skip rendering when the
    /// set of active layers is also unchanged (not just the layer definitions).
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

    /// Check if the entire scene is effectively static at the given frame.
    [[nodiscard]] bool is_effectively_static_at(const Scene& scene, Frame frame) const;
};

} // namespace chronon3d::graph
