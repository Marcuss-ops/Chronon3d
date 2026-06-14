#pragma once

#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
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

    /// Frame-independent structure fingerprint used to decide whether a cached
    /// compiled graph can be safely reused.  This intentionally ignores the
    /// per-node render payload (for example changing text content) while still
    /// tracking the layer topology and the graph-affecting layer settings.
    uint64_t compute_structure_fingerprint(const Scene& scene) {
        uint64_t h = 0;
        h = hash_combine(h, hash_string("scene_root_structure"));
        h = hash_combine(h, hash_value(scene.nodes().size()));
        for (const auto& node : scene.nodes()) {
            h = hash_combine(h, hash_string(node.name));
        }

        for (const auto& layer : scene.layers()) {
            const auto layer_h = hash_layer_structure(layer);
            h = hash_combine(h, layer_h);
            h = hash_combine(h, hash_value(layer.nodes.size()));
        }

        return h;
    }

    /// Returns true if the scene is "static" — i.e. safe to reuse the same
    /// framebuffer across consecutive frames. A scene is static when:
    ///   - No layer has AnimatedTransform with actual keyframes or expressions
    ///   - No layer is a Video layer (video sources are inherently frame-dependent)
    ///   - No layer has time-dependent expressions in its AnimatedValues
    ///   - Camera has no animation
    /// When static, we relax the m_prev_frame == frame check to allow
    /// m_prev_frame == frame - 1 (consecutive frames), enabling the fast-path
    /// to work in real video exports (frames 0,1,2,3...) not just benchmarks.
    bool is_static_scene(const Scene& scene) const;

    /// Frame-aware variant: returns true if the scene is static AT the given
    /// frame (i.e., the scene output won't change between frame and frame+1).
    /// Unlike is_static_scene() which rejects ANY layer with keyframes, this
    /// accounts for animations that have reached their terminal state.
    /// For example, tracking_breathing(1.04f, Frame{120}) on a 150-frame comp:
    /// frames 120-149 are effectively static (scale has settled at 1.0).
    bool is_static_scene_at(const Scene& scene, Frame frame) const {
        return is_effectively_static_at(scene, frame);
    }

    /// Frame-dependent fingerprint that captures which layers are active at a
    /// specific frame. Unlike compute_static_fingerprint() (which hashes all
    /// layers unconditionally), this incorporates active_at(frame) — so scenes
    /// where layers activate/deactivate across frames produce different fingerprints.
    /// Used by the static fast-path to ensure we only skip rendering when the
    /// set of active layers is also unchanged (not just the layer definitions).
    uint64_t compute_active_at_fingerprint(const Scene& scene, Frame frame) const {
        uint64_t h = 0;
        h = hash_combine(h, hash_string("active_at"));
        for (const auto& layer : scene.layers()) {
            // Hash both the layer identity and its active state at this frame
            h = hash_combine(h, hash_string(layer.name));
            h = hash_combine(h, layer.active_at(frame) ? 1 : 0);
        }
        return h;
    }

    void clear() {
        // No cached state to clear — hashing is stateless.
    }

private:
    static uint64_t hash_layer(const Layer& layer) {
        uint64_t h = 0;
        h = hash_combine(h, hash_string(layer.name));
        h = hash_combine(h, static_cast<u64>(layer.kind));
        h = hash_combine(h, layer.visible ? 1 : 0);
        h = hash_combine(h, layer.uses_2_5d_projection ? 1 : 0);
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
        if (layer.kind == LayerKind::Video && layer.video_source) {
            h = hash_combine(h, hash_string(layer.video_source->path));
            h = hash_combine(h, hash_vec2(layer.video_source->size));
            h = hash_combine(h, hash_value(layer.video_source->source_fps));
        }
        // Time Remap (AE-4): include speed and freeze_frame in content hash
        if (layer.time_remap.active()) {
            h = hash_combine(h, hash_value(layer.time_remap.speed));
            h = hash_combine(h, hash_value(static_cast<u64>(layer.time_remap.freeze_frame)));
        }
        return h;
    }

    /// Structure fingerprint: only topology-affecting properties.
    /// Intentionally excludes animated transform values, mask parameters,
    /// and per-node render payloads (text content, effect params, etc.)
    /// so that scenes with animated transforms can reuse the compiled graph
    /// across frames without a full rebuild.
    static uint64_t hash_layer_structure(const Layer& layer) {
        uint64_t h = 0;
        h = hash_combine(h, hash_string(layer.name));
        h = hash_combine(h, static_cast<u64>(layer.kind));
        h = hash_combine(h, layer.visible ? 1 : 0);
        h = hash_combine(h, layer.uses_2_5d_projection ? 1 : 0);
        h = hash_combine(h, layer.cache_static ? 1 : 0);
        h = hash_combine(h, static_cast<u64>(layer.blend_mode));
        // NOTE: hash_transform and hash_mask are intentionally excluded.
        // Transform values (position, scale, rotation, opacity) change every
        // frame during animation, which would prevent graph reuse. The graph
        // topology is unaffected by transform values — only the node payloads
        // change, which is handled by refresh_compiled_graph_payloads().

        if (layer.kind == LayerKind::Precomp) {
            h = hash_combine(h, hash_string(layer.precomp_composition_name));
        }
        if (layer.kind == LayerKind::Video && layer.video_source) {
            h = hash_combine(h, hash_string(layer.video_source->path));
            h = hash_combine(h, hash_vec2(layer.video_source->size));
            h = hash_combine(h, hash_value(layer.video_source->source_fps));
        }
        // Time Remap (AE-4): include in structure hash
        if (layer.time_remap.active()) {
            h = hash_combine(h, hash_value(layer.time_remap.speed));
            h = hash_combine(h, hash_value(static_cast<u64>(layer.time_remap.freeze_frame)));
        }
        return h;
    }

    /// Check if a layer's animation is effectively stable at the given frame.
    /// Returns true when all animated values have reached their terminal state
    /// (i.e., evaluating at frame and frame+1 would produce the same result).
    /// For example, tracking_breathing(1.04f, Frame{120}) on frame 120+ returns
    /// true because the scale has settled at 1.0 and won't change further.
    /// NOTE: Loop/PingPong animations NEVER reach terminal state — they wrap
    /// around and produce different values at frame vs frame+1 even when past
    /// the last keyframe time. Those are correctly rejected here.
    static bool layer_animation_done_at(const Layer& layer, Frame frame) {
        // If not time-dependent at all, it's always stable
        if (!layer.anim_transform.is_time_dependent()) return true;

        // Helper: check if a single AnimatedValue has reached its terminal state.
        // Expression-only properties (no keyframes) are never done — they change every frame.
        auto is_done = [&](const auto& val) -> bool {
            if (!val.is_time_dependent()) return true;   // not time-dependent → always stable
            if (!val.is_animated()) return false;         // expression-only → never done
            if (val.loop_mode() != LoopMode::Hold) return false; // looping → never done
            return val.last_keyframe_time() <= frame;        // Hold + past last keyframe → stable
        };

        return is_done(layer.anim_transform.position) &&
               is_done(layer.anim_transform.scale) &&
               is_done(layer.anim_transform.rotation_euler) &&
               is_done(layer.anim_transform.anchor) &&
               is_done(layer.anim_transform.opacity) &&
               is_done(layer.anim_transform.blur);
    }

    static bool layer_is_static(const Layer& layer) {
        if (layer.kind == LayerKind::Video) return false; // Video is time-dependent
        if (layer.kind == LayerKind::Precomp) return false; // Precomp may reference animated compositions
        if (layer.anim_transform.is_time_dependent()) return false; // Time-dependent transform
        // Transitions make a layer frame-dependent
        if (layer.transition_in.duration > 0 || layer.transition_out.duration > 0) return false; // Non-zero transitions are frame-dependent
        // Time Remap (AE-4): time-dependent time_remap makes layer frame-dependent
        if (layer.time_remap.time_remap.is_time_dependent()) return false;
        return true;
    }

    /// Frame-aware version: returns true if the layer is effectively static at
    /// the given frame — no active animation producing different values between
    /// consecutive frames.
    static bool layer_is_static_at(const Layer& layer, Frame frame) {
        if (layer.kind == LayerKind::Video) return false;
        if (layer.kind == LayerKind::Precomp) return false;
        // If animation is done (all keyframes passed), layer is effectively static
        if (layer.anim_transform.is_time_dependent() && !layer_animation_done_at(layer, frame)) {
            return false;
        }
        if (layer.transition_in.duration > 0 || layer.transition_out.duration > 0) return false;
        // Time Remap (AE-4): time-dependent time_remap makes layer frame-dependent
        if (layer.time_remap.time_remap.is_time_dependent()) return false;
        return true;
    }

    static bool camera_is_static(const Camera2_5DRuntime& cam) {
        // AnimatedCamera2_5D::evaluate() sets `is_animated = true` whenever
        // any of the camera properties (position, rotation, zoom, fov, dof)
        // is driven by keyframed or expression-based animation.  This flag
        // is the source of truth for "is this camera time-dependent?" — it
        // lets the static-scene fast-path (compute_fingerprint reuse, frame
        // skipping) correctly reject scenes with moving cameras without
        // having to inspect the underlying AnimatedValue<T> machinery.
        // Disabled cameras (cam.enabled == false) are always static.
        if (!cam.enabled) return true;
        return !cam.is_animated;
    }

    /// Check if the entire scene is effectively static at the given frame.
    bool is_effectively_static_at(const Scene& scene, Frame frame) const {
        for (const auto& layer : scene.layers()) {
            if (!layer_is_static_at(layer, frame)) return false;
        }
        if (!camera_is_static(scene.camera_2_5d())) return false;
        return true;
    }

};

// Free function implementing is_static_scene check across all scene layers.
inline bool SceneHasher::is_static_scene(const Scene& scene) const {
    for (const auto& layer : scene.layers()) {
        if (!layer_is_static(layer)) return false;
    }
    if (!camera_is_static(scene.camera_2_5d())) return false;
    return true;
}

} // namespace chronon3d::graph
