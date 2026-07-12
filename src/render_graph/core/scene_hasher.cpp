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

[[nodiscard]] bool SceneHasher::is_effectively_static_at(const Scene& scene, Frame frame) const {
    for (const auto& layer : scene.layers()) {
        if (!layer_is_static_at(layer, frame)) return false;
    }
    if (!camera_is_static(scene.camera_2_5d())) return false;
    return true;
}

} // namespace chronon3d::graph
