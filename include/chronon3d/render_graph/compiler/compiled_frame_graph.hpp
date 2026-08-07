#pragma once

#include <chronon3d/internal/render_graph/render_graph.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/render_graph/core/node_identity.hpp>
#include <chronon3d/internal/render_graph/processor_registry_snapshot.hpp>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace chronon3d::graph {
using NodeCacheKey = ::chronon3d::cache::NodeCacheKey;

// ── Binding metadata ───────────────────────────────────────────────────
// Attached to CompiledNodeInfo during graph build/compilation.
// The binding compiler reads this to build the binding table.
struct SceneBindingMetadata {
    bool     active{false};       // explicitly opt-in; avoids layer-0/item-0 ambiguity
    uint32_t layer_index{0};
    uint32_t item_index{0};
    uint16_t effect_begin{0};
    uint16_t effect_count{0};

    [[nodiscard]] bool has_binding() const { return active; }
};

struct CompiledNodeInfo {
    GraphNodeId id{k_invalid_node};

    RenderGraphNodeKind kind{};
    std::string name;
    std::string layer_id;

    std::vector<GraphNodeId> inputs;
    std::vector<GraphNodeId> consumers;

    NodeCacheKey static_key{};
    /// Canonical cache contract (replaces the deleted `frame_dependent` /
    /// `cacheable` / `disk_cacheable` derived bools — do not re-introduce).
    RenderNodeCachePolicy cache_policy{};

    // Structural render payload discriminator captured at compile time.
    // -1 means the node has no shape payload; -2 means an aggregate source.
    // Dynamic refresh may replace payload values, but never this discriminator.
    int shape_type{-1};
    std::vector<int> source_shape_types;

    // Stable processor family/type identity captured at compilation. This is
    // structural metadata used by scene_refresh validation; per-frame
    // payload refresh must never replace it.
    std::string processor_id;

    // Processor bindings are immutable handles into the graph-owned
    // ProcessorRegistrySnapshot. The executor uses the graph-level
    // pre-resolved pointer tables below, never the mutable registry.
    renderer::ShapeProcessorHandle shape_processor{};
    // Offset/count into CompiledFrameGraph::shape_processor_table. For a
    // single source the count is one; multi-source entries retain null slots
    // for TextRun items so authored item indices remain aligned.
    std::uint32_t shape_processors_offset{0};
    std::uint32_t shape_processors_count{0};
    std::uint32_t effect_processors_offset{0};
    std::uint32_t effect_processors_count{0};

    SceneBindingMetadata binding_meta{};  // binding table metadata

    bool reachable{false};
    bool early_exit_skip{false};

    std::optional<raster::BBox> predicted_bbox;

    // ── Work Package 4 — stable identity ────────────────────────────
    // Populated by `FrameGraphCompiler::build_node_metadata` from a
    // deterministic mix of `layer_id`, `kind`, and `name`.  Excludes
    // addresses, timestamps, and unordered iteration.  Two distinct
    // reachable nodes can NEVER collide on this id; the compiler
    // throws `std::runtime_error` on collision (PR 4.3).
    StableNodeId stable_node_id{kInvalidStableNodeId};
};

struct ResourceLifetime {
    GraphNodeId producer{k_invalid_node};
    std::size_t first_level{0};
    std::size_t last_level{0};
    std::size_t consumer_count{0};
    bool can_release_after_last_consumer{true};
};

struct CompiledFrameGraph {
    RenderGraph graph;
    GraphNodeId output{k_invalid_node};

    std::uint64_t structure_hash{0};

    // Registry generation and immutable ownership used to resolve compiled
    // processor handles. The snapshot keeps processors alive after the
    // originating SoftwareRegistry or engine is destroyed.
    std::uint64_t registry_generation{0};
    std::uint64_t processor_snapshot_identity{0};
    std::shared_ptr<const renderer::ProcessorRegistrySnapshot> processor_snapshot;

    // Immutable handle tables populated once at compile time. Raw processor
    // addresses are never persisted in the compiled graph; they are resolved
    // only at the final backend dispatch boundary through processor_snapshot.
    std::vector<renderer::ShapeProcessorHandle> shape_processor_table;
    std::vector<renderer::EffectProcessorHandle> effect_processor_table;

    // Authored-scene topology fingerprint captured by the coordinator when
    // this compiled graph was built. It is compared before refresh so an
    // incorrect scene-structure hint cannot reuse an incompatible graph.
    std::uint64_t authored_structure_fingerprint{0};

    std::vector<std::vector<GraphNodeId>> levels;
    std::vector<std::size_t> consumer_counts;

    std::vector<CompiledNodeInfo> nodes;
    std::vector<ResourceLifetime> lifetimes;

    std::vector<bool> early_exit_skip;
    bool skip_initial_clear{false};

    bool valid{false};

    // ── Work Package 4 — stable identity ────────────────────────────
    // Built by `FrameGraphCompiler::compile` by hashing the SET of
    // stable_node_ids of every reachable node (FNV-1a determinism).
    // Two graphs with identical topology AND identical reachable-node
    // identities produce the same `graph_instance_id`; nested
    // compiled graphs (precomp layers) get UNIQUE ids because the
    // compiler is invoked separately for each precomp layer.
    GraphInstanceId graph_instance_id{kInvalidGraphInstanceId};

    [[nodiscard]] bool empty() const {
        return !valid || levels.empty() || output == k_invalid_node;
    }

    [[nodiscard]] NodeIdentity node_identity(GraphNodeId id) const noexcept {
        if (id >= nodes.size()) {
            return NodeIdentity{};
        }
        return NodeIdentity{graph_instance_id, nodes[id].stable_node_id};
    }
};

} // namespace chronon3d::graph
