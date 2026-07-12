// =============================================================================
// src/render_graph/core/scene_hasher.cpp
//
// Implementation bodies for SceneHasher.  Moved here from the header
// to break a circular include dependency:
//   scene.hpp → camera_program.hpp → render_session.hpp → scene_hasher.hpp
//   → scene.hpp (blocked by #pragma once, leaving Scene incomplete).
//
// TICKET-BUILD-ROT-CASCADE-CAMERA surface B.
// =============================================================================

#include <chronon3d/internal/render_graph/core/scene_hasher.hpp>

// Full definitions needed by the method bodies (only forward-declared in the header).
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>

namespace chronon3d::graph {

// ── Public fingerprinting methods ───────────────────────────────────────────

uint64_t SceneHasher::compute_fingerprint(const Scene& scene, Frame frame) {
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

uint64_t SceneHasher::compute_static_fingerprint(const Scene& scene) {
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

uint64_t SceneHasher::compute_structure_fingerprint(const Scene& scene) {
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

[[nodiscard]] bool SceneHasher::is_static_scene(const Scene& scene) const {
    for (const auto& layer : scene.layers()) {
        if (!layer_is_static(layer)) return false;
    }
    if (!camera_is_static(scene.camera_2_5d())) return false;
    return true;
}

[[nodiscard]] bool SceneHasher::is_static_scene_at(const Scene& scene, Frame frame) const {
    return is_effectively_static_at(scene, frame);
}

uint64_t SceneHasher::compute_active_at_fingerprint(const Scene& scene, Frame frame) const {
    uint64_t h = 0;
    h = hash_combine(h, hash_string("active_at"));
    for (const auto& layer : scene.layers()) {
        h = hash_combine(h, hash_string(layer.name));
        h = hash_combine(h, layer.active_at(frame) ? 1 : 0);
    }
    return h;
}

// ── Private helpers ─────────────────────────────────────────────────────────

uint64_t SceneHasher::hash_layer(const Layer& layer) {
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
    h = hash_combine(h, static_cast<u64>(layer.track_matte.type));
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

uint64_t SceneHasher::hash_layer_structure(const Layer& layer) {
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

bool SceneHasher::layer_animation_done_at(const Layer& layer, Frame frame) {
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

bool SceneHasher::layer_is_static(const Layer& layer) {
    if (layer.kind == LayerKind::Video) return false;
    if (layer.kind == LayerKind::Precomp) return false;
    if (layer.anim_transform.is_time_dependent()) return false;
    if (layer.transition_in.duration > 0 || layer.transition_out.duration > 0) return false;
    if (layer.time_remap.time_remap.is_time_dependent()) return false;
    return true;
}

bool SceneHasher::layer_is_static_at(const Layer& layer, Frame frame) {
    if (layer.kind == LayerKind::Video) return false;
    if (layer.kind == LayerKind::Precomp) return false;
    if (layer.anim_transform.is_time_dependent() && !layer_animation_done_at(layer, frame)) {
        return false;
    }
    if (layer.transition_in.duration > 0 || layer.transition_out.duration > 0) return false;
    if (layer.time_remap.time_remap.is_time_dependent()) return false;
    return true;
}

bool SceneHasher::camera_is_static(const Camera2_5DRuntime& cam) {
    if (!cam.enabled) return true;
    return !cam.is_animated;
}

[[nodiscard]] bool SceneHasher::is_effectively_static_at(const Scene& scene, Frame frame) const {
    for (const auto& layer : scene.layers()) {
        if (!layer_is_static_at(layer, frame)) return false;
    }
    if (!camera_is_static(scene.camera_2_5d())) return false;
    return true;
}

} // namespace chronon3d::graph
