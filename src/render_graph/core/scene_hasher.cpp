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
#include <chronon3d/scene/model/core/effect_stack.hpp>

namespace chronon3d::graph {

namespace {

uint64_t hash_clip_transition(const SceneClipTransition& transition) {
    uint64_t h = hash_string(transition.layer_a);
    h = hash_combine(h, hash_string(transition.layer_b));
    h = hash_combine(h, hash_value(static_cast<uint64_t>(transition.spec.kind)));
    h = hash_combine(h, hash_value(static_cast<uint64_t>(transition.spec.easing)));
    h = hash_combine(h, hash_value(static_cast<uint64_t>(transition.spec.fit)));
    h = hash_combine(h, hash_value(static_cast<uint64_t>(transition.spec.direction)));
    h = hash_combine(h, hash_vec2(transition.spec.center));
    h = hash_combine(h, hash_value(transition.spec.feather));
    h = hash_combine(h, hash_color(transition.spec.flash_color));
    h = hash_combine(h, hash_value(transition.spec.zoom_scale));
    h = hash_combine(h, hash_value(static_cast<int64_t>(transition.from)));
    h = hash_combine(h, hash_value(static_cast<int64_t>(transition.duration)));
    return h;
}

// Topology-only variants deliberately omit transition timing and visual
// parameters. Those values affect per-frame evaluation, not the emitted
// node/edge topology, and are handled by the dynamic refresh path.
uint64_t hash_layer_transition_topology(const LayerTransitionSpec& spec) {
    u64 h = hash_string(spec.transition_id);
    return hash_combine(h, spec.transition_id == "none" ? 0ULL : 1ULL);
}

uint64_t hash_clip_transition_topology(const SceneClipTransition& transition) {
    u64 h = hash_string(transition.layer_a);
    h = hash_combine(h, hash_string(transition.layer_b));
    return hash_combine(h, hash_value(static_cast<uint64_t>(transition.spec.kind)));
}

uint64_t clip_transition_phase(const SceneClipTransition& transition, Frame frame) {
    const Frame duration = transition.duration > 0 ? transition.duration : Frame{1};
    const Frame end = transition.from + duration;

    if (frame < transition.from) return 0;
    if (frame >= end) return static_cast<uint64_t>(static_cast<int64_t>(duration)) + 1;
    return static_cast<uint64_t>(static_cast<int64_t>(frame - transition.from)) + 1;
}

} // namespace

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

    for (const auto& transition : scene.clip_transitions()) {
        h = hash_combine(h, hash_clip_transition(transition));
        h = hash_combine(h, clip_transition_phase(transition, frame));
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

    for (const auto& transition : scene.clip_transitions()) {
        h = hash_combine(h, hash_clip_transition(transition));
    }

    return h;
}

uint64_t SceneHasher::compute_structure_fingerprint(
    const Scene& scene,
    uint64_t registry_generation) {
    uint64_t h = 0;
    h = hash_combine(h, hash_string("chronon.scene-topology.v2"));
    h = hash_combine(h, hash_string("registry-generation"));
    h = hash_combine(h, registry_generation);

    // Root-source order and identity are graph topology: root sources are
    // emitted in this order by the builder.  Do not fold their render values
    // (placement, colour, fill, opacity, or shape dimensions) here.
    h = hash_combine(h, hash_value(scene.nodes().size()));
    for (const auto& node : scene.nodes()) {
        h = hash_combine(h, hash_string(node.name));
        h = hash_combine(h, hash_value(static_cast<int>(node.shape.type())));
        h = hash_combine(h, hash_value(static_cast<int>(node.surface_policy)));
        h = hash_combine(h, hash_value(static_cast<int>(node.transform_policy)));
    }

    // Preserve authored layer order.  The graph builder composites layers in
    // this order (with a separate deterministic 2.5D sort at execution), so
    // sorting here would make two different graphs look cache-compatible.
    h = hash_combine(h, hash_value(scene.layers().size()));
    for (const auto& layer : scene.layers()) {
        h = hash_combine(h, hash_layer_structure(layer));
        h = hash_combine(h, hash_string(layer.parent_name));
        h = hash_combine(h, hash_value(layer.nodes.size()));
        for (const auto& node : layer.nodes) {
            // Node order, stable name and render-kind discriminator determine
            // whether the source pass emits SourceNode, TextRunNode, or a
            // MultiSource item.  All animated/render payload values stay out.
            h = hash_combine(h, hash_string(node.name));
            h = hash_combine(h, hash_value(static_cast<int>(node.shape.type())));
            h = hash_combine(h, hash_value(static_cast<int>(node.surface_policy)));
            h = hash_combine(h, hash_value(static_cast<int>(node.transform_policy)));
            // The authored node's processor family is its structural shape
            // discriminator. Input edges are represented by authored order
            // and parent/track-matte identities below; dynamic payloads stay
            // excluded from this fingerprint.
        }

        // The builder emits one graph node per enabled effect.  Effect
        // parameters are refreshed dynamically, so only enabled/type/id are
        // part of this topology contract.
        // Disabled effects do not emit graph nodes in the builder, so they
        // must not perturb the topology fingerprint. Preserve the authored
        // order of enabled effects and hash only their processor identity;
        // effect parameters are dynamic payload and are refreshed later.
        u64 enabled_effect_count = 0;
        for (const auto& effect : layer.effects()) {
            if (effect.enabled) ++enabled_effect_count;
        }
        h = hash_combine(h, enabled_effect_count);
        for (const auto& effect : layer.effects()) {
            if (!effect.enabled) continue;
            h = hash_combine(h, hash_string(effect.descriptor.id));
            h = hash_combine(h, hash_value(static_cast<int>(effect.effect_type)));
        }
    }

    // Clip transitions add nodes and edges.  Their authored identity and
    // kind are structural; timing, easing, colours and the current phase are
    // dynamic transition payload and intentionally omitted.
    for (const auto& transition : scene.clip_transitions()) {
        h = hash_combine(h, hash_clip_transition_topology(transition));
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
        // A video layer's sampled source frame advances every output frame,
        // so its content is time-varying even while the layer stays active.
        // Fold the current frame into the fingerprint so the frame-reuse fast
        // path never replays a stale video frame (GOLDEN 04/05 light leak).
        if (layer.kind == LayerKind::Video && layer.video_source &&
            layer.active_at(frame)) {
            h = hash_combine(h, static_cast<int64_t>(frame));
        }
    }
    for (const auto& transition : scene.clip_transitions()) {
        h = hash_combine(h, hash_string(transition.layer_a));
        h = hash_combine(h, hash_string(transition.layer_b));
        h = hash_combine(h, clip_transition_phase(transition, frame));
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
    h = hash_combine(h, layer.screen_space ? 1 : 0);
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
    h = hash_combine(h, layer.uses_2_5d_projection ? 1 : 0);
    h = hash_combine(h, layer.screen_space ? 1 : 0);
    // Cache policy is compile-time graph metadata (it changes the node
    // cache policy captured by Source/Transform/Effect nodes), not a
    // per-frame render value.
    h = hash_combine(h, layer.cache_static ? 1ULL : 0ULL);
    h = hash_combine(h, static_cast<u64>(layer.blend_mode));
    // The current compositor contract emits SourceOver for this pass;
    // do not invalidate the graph on an authored operator that the builder
    // does not consume yet.
    h = hash_combine(h, static_cast<u64>(layer.track_matte.type));
    if (layer.track_matte.active()) {
        h = hash_combine(h, hash_string(layer.track_matte.source_layer));
    }
    h = hash_combine(h, layer.mask.enabled() ? 1ULL : 0ULL);
    if (layer.mask.enabled()) {
        h = hash_combine(h, static_cast<u64>(layer.mask.type));
    }

    // A transition changes the emitted node chain.  Include its authored
    // identity and static parameters, but not any evaluated frame phase.
    const auto has_transition = [](const LayerTransitionSpec& spec) {
        return !spec.transition_id.empty() && spec.transition_id != "none";
    };
    h = hash_combine(h, has_transition(layer.transition_in) ? 1ULL : 0ULL);
    if (has_transition(layer.transition_in)) {
        h = hash_combine(h, hash_layer_transition_topology(layer.transition_in));
    }
    h = hash_combine(h, has_transition(layer.transition_out) ? 1ULL : 0ULL);
    if (has_transition(layer.transition_out)) {
        h = hash_combine(h, hash_layer_transition_topology(layer.transition_out));
    }

    if (layer.kind == LayerKind::Precomp) {
        h = hash_combine(h, hash_string(layer.precomp_composition_name));
    }
    // Video source identity and time-remap values are dynamic payload; the
    // VideoNode topology is unchanged and refresh evaluates the sample.
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
    for (const auto& transition : scene.clip_transitions()) {
        const Frame duration = transition.duration > 0 ? transition.duration : Frame{1};
        if (frame >= transition.from && frame < transition.from + duration) {
            return false;
        }
    }
    if (!camera_is_static(scene.camera_2_5d())) return false;
    return true;
}

} // namespace chronon3d::graph
