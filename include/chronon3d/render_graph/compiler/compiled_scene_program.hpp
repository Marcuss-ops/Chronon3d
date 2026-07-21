#pragma once

// ──────────────────────────────────────────────────────────────────────────────
// compiled_scene_program.hpp — B5: CompiledSceneProgram and binding table
//
// A CompiledSceneProgram pairs a CompiledFrameGraph with a binding table
// that maps each layer item (transform, source, effect stack, matte, etc.)
// to the corresponding graph node.  The refresh function walks the binding
// table rather than scanning all graph nodes with per-name lookups.
//
// The SceneStructureKey identifies when the program must be recompiled:
//   topology_hash     — layer order, LayerKind, blend mode, matte, effect IDs
//   active_set_hash   — which layers are active at a given frame
//   render_options_hash — resolution, SSAA, renderer flags
//
// When all three match an existing CompiledSceneProgram, the cached program
// can be reused and only the per-frame parameter block needs refreshing.
// ──────────────────────────────────────────────────────────────────────────────

#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>
#include <chronon3d/core/types/types.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace chronon3d::graph {

// ── Binding kinds (B5) ───────────────────────────────────────────────────────
//
/// Describes what type of scene element a binding entry represents.
/// Each entry maps a GraphNodeId to a specific layer item, so the refresh
/// function can directly update the correct node without scanning.
enum class SceneBindingKind : uint8_t {
    Source,           // single-node layer source
    MultiSource,      // layer with multiple render nodes
    Transform,        // layer transform (position, rotation, scale, opacity)
    EffectStack,      // layer effect stack (possibly merged by optimizer)
    TrackMatte,       // track matte node
    Transition,       // layer transition node
    Adjustment        // adjustment layer
};

/// Human-readable name for SceneBindingKind (for diagnostics).
[[nodiscard]] inline const char* to_string(SceneBindingKind kind) {
    switch (kind) {
        case SceneBindingKind::Source:      return "Source";
        case SceneBindingKind::MultiSource: return "MultiSource";
        case SceneBindingKind::Transform:   return "Transform";
        case SceneBindingKind::EffectStack: return "EffectStack";
        case SceneBindingKind::TrackMatte:  return "TrackMatte";
        case SceneBindingKind::Transition:  return "Transition";
        case SceneBindingKind::Adjustment:  return "Adjustment";
    }
    return "Unknown";
}

// ── SceneBinding ─────────────────────────────────────────────────────────────
//
/// A single entry in the binding table, tying a graph node to a layer item.
struct SceneBinding {
    SceneBindingKind kind{SceneBindingKind::Source};
    GraphNodeId      node_id{k_invalid_node};

    uint32_t layer_index{0};    // index into resolved layers
    uint32_t item_index{0};     // item index within the layer

    uint16_t effect_begin{0};   // first effect index in Layer::effects[]
    uint16_t effect_count{0};   // number of effects (0 if not an effect node)
};

// ── SceneStructureKey ────────────────────────────────────────────────────────
//
/// Identifies when the compiled program must be rebuilt from scratch.
/// Excludes per-frame dynamic values (transform, opacity, effect params).
struct SceneStructureKey {
    uint64_t topology_hash{0};        // layer order, kind, blend mode, matte, effect IDs
    uint64_t active_set_hash{0};      // which layers are active at the current frame
    uint64_t render_options_hash{0};  // resolution, SSAA, renderer flags

    int width{0};
    int height{0};
    int ssaa_factor{1};

    bool operator==(const SceneStructureKey& other) const noexcept {
        return topology_hash == other.topology_hash &&
               active_set_hash == other.active_set_hash &&
               render_options_hash == other.render_options_hash &&
               width == other.width &&
               height == other.height &&
               ssaa_factor == other.ssaa_factor;
    }

    bool operator!=(const SceneStructureKey& other) const noexcept {
        return !(*this == other);
    }

    /// Returns true when the key has been initialized (non-zero hashes).
    [[nodiscard]] bool valid() const noexcept {
        return topology_hash != 0 || active_set_hash != 0 || render_options_hash != 0;
    }
};

} // namespace chronon3d::graph

// ── std::hash<SceneStructureKey> ────────────────────────────────────────────
namespace std {
template <>
struct hash<::chronon3d::graph::SceneStructureKey> {
    size_t operator()(const ::chronon3d::graph::SceneStructureKey& key) const noexcept {
        size_t h = 1469598103934665603ULL;  // FNV-1a offset basis
        auto combine = [&](uint64_t v) {
            h ^= static_cast<size_t>(v);
            h *= 1099511628211ULL;
        };
        combine(key.topology_hash);
        combine(key.active_set_hash);
        combine(key.render_options_hash);
        combine(static_cast<uint64_t>(key.width));
        combine(static_cast<uint64_t>(key.height));
        combine(static_cast<uint64_t>(key.ssaa_factor));
        return h;
    }
};
} // namespace std

namespace chronon3d::graph {

// ── CompiledSceneProgram ─────────────────────────────────────────────────────
//
/// The complete output of scene compilation: a compiled frame graph, its
/// structure identity key, and the binding table for efficient refresh.
struct CompiledSceneProgram {
    CompiledFrameGraph          frame_graph;
    SceneStructureKey           key;
    std::vector<SceneBinding>   bindings;

    bool valid{false};

    [[nodiscard]] bool empty() const {
        return !valid || frame_graph.empty();
    }

    void clear() {
        frame_graph = CompiledFrameGraph{};
        key = SceneStructureKey{};
        bindings.clear();
        valid = false;
    }
};

} // namespace chronon3d::graph
