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
    static uint64_t hash_layer(const Layer& layer) {
        uint64_t h = 0;
        h = hash_combine(h, hash_string(layer.name));
        h = hash_combine(h, hash_string(layer.parent_name));
        h = hash_combine(h, static_cast<u64>(layer.kind));
        h = hash_combine(h, layer.visible ? 1 : 0);
        h = hash_combine(h, layer.uses_2_5d_projection ? 1 : 0);
        h = hash_combine(h, layer.cache_static ? 1 : 0);
        h = hash_combine(h, static_cast<u64>(layer.blend_mode));
        h = hash_combine(h, static_cast<u64>(layer.composite_operator));
        h = hash_combine(h, hash_transform(layer.transform));
        h = hash_combine(h, hash_mask(layer.mask));
        if (layer.m_effects) {
            h = hash_combine(h, hash_effect_stack(*layer.m_effects));
        }
        for (const auto& node : layer.nodes) {
            h = hash_combine(h, hash_render_node(node));
        }
        if (layer.kind == LayerKind::Precomp) {
            h = hash_combine(h, hash_string(layer.precomp_composition_name));
        }
        if (layer.kind == LayerKind::Video && layer.video_source) {
            h = hash_combine(h, hash_string(layer.video_source->path));
            h = hash_combine(h, hash_vec2(layer.video_source->size));
            h = hash_combine(h, hash_value(layer.video_source->source_fps));
        }
        if (layer.time_remap.active()) {
            h = hash_combine(h, hash_value(layer.time_remap.speed));
            h = hash_combine(h, hash_value(static_cast<u64>(layer.time_remap.freeze_frame)));
        }
        return h;
    }

    static uint64_t hash_layer_structure(const Layer& layer) {
        uint64_t h = 0;
        h = hash_combine(h, hash_string(layer.name));
        h = hash_combine(h, static_cast<u64>(layer.kind));
        h = hash_combine(h, layer.visible ? 1 : 0);
        h = hash_combine(h, layer.uses_2_5d_projection ? 1 : 0);
        h = hash_combine(h, layer.cache_static ? 1 : 0);
        h = hash_combine(h, static_cast<u64>(layer.blend_mode));
        if (layer.kind == LayerKind::Precomp) {
            h = hash_combine(h, hash_string(layer.precomp_composition_name));
        }
        if (layer.kind == LayerKind::Video && layer.video_source) {
            h = hash_combine(h, hash_string(layer.video_source->path));
            h = hash_combine(h, hash_vec2(layer.video_source->size));
            h = hash_combine(h, hash_value(layer.video_source->source_fps));
        }
        if (layer.time_remap.active()) {
            h = hash_combine(h, hash_value(layer.time_remap.speed));
            h = hash_combine(h, hash_value(static_cast<u64>(layer.time_remap.freeze_frame)));
        }
        return h;
    }

    static bool layer_animation_done_at(const Layer& layer, Frame frame) {
        if (!layer.anim_transform.is_time_dependent()) return true;
        if (layer.anim_transform.position.has_expression() ||
            layer.anim_transform.scale.has_expression() ||
            layer.anim_transform.rotation_euler.has_expression() ||
            layer.anim_transform.anchor.has_expression() ||
            layer.anim_transform.opacity.has_expression() ||
            layer.anim_transform.blur.has_expression()) {
            return false;
        }
        auto is_done = [&](const auto& val) -> bool {
            if (!val.is_time_dependent()) return true;
            if (!val.is_animated()) return false;
            if (val.loop_mode() != LoopMode::Hold) return false;
            return val.last_keyframe_time() <= frame;
        };
        return is_done(layer.anim_transform.position) &&
               is_done(layer.anim_transform.scale) &&
               is_done(layer.anim_transform.rotation_euler) &&
               is_done(layer.anim_transform.anchor) &&
               is_done(layer.anim_transform.opacity) &&
               is_done(layer.anim_transform.blur);
    }

    static bool layer_is_static(const Layer& layer) {
        if (layer.kind == LayerKind::Video) return false;
        if (layer.kind == LayerKind::Precomp) return false;
        if (layer.anim_transform.is_time_dependent()) return false;
        if (layer.transition_in.duration > 0 || layer.transition_out.duration > 0) return false;
        if (layer.time_remap.time_remap.is_time_dependent()) return false;
        return true;
    }

    static bool layer_is_static_at(const Layer& layer, Frame frame) {
        if (layer.kind == LayerKind::Video) return false;
        if (layer.kind == LayerKind::Precomp) return false;
        if (layer.anim_transform.is_time_dependent() && !layer_animation_done_at(layer, frame)) {
            return false;
        }
        if (layer.transition_in.duration > 0 || layer.transition_out.duration > 0) return false;
        if (layer.time_remap.time_remap.is_time_dependent()) return false;
        return true;
    }

    static bool camera_is_static(const Camera2_5DRuntime& cam) {
        if (!cam.enabled) return true;
        return !cam.is_animated;
    }

    /// Check if the entire scene is effectively static at the given frame.
    [[nodiscard]] bool is_effectively_static_at(const Scene& scene, Frame frame) const;
};

} // namespace chronon3d::graph
