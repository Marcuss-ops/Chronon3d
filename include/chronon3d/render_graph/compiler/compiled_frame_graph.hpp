#pragma once

#include <chronon3d/internal/render_graph/render_graph.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/render_graph/core/node_identity.hpp>
#include <chronon3d/render_graph/compiler/physical_framebuffer_allocation.hpp>
#include <chronon3d/render_graph/pipeline/frame_parameter_table.hpp>
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
    bool lowered_into_batch{false};

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

struct CompiledOwnershipTransfer {
    GraphNodeId producer{k_invalid_node};
    GraphNodeId consumer{k_invalid_node};
    bool transferable{false};
};

// ── Compiled execute function type ───────────────────────────────────────
//
// When non-null, the operation can be executed without calling
// node.execute().  The function receives:
//   - backend: the render backend
//   - op: this CompiledOperation
// Returns true on success.
class RenderBackend;
using CompiledExecuteFn = bool (*)(RenderBackend* backend,
                                   const struct CompiledOperation& op);

// Linear, domain-neutral execution description. Processors that do not yet
// provide a compiled recorder remain valid through node.execute() fallback.
struct CompiledOperation {
    GraphNodeId node{k_invalid_node};
    StableNodeId stable_node{kInvalidStableNodeId};
    std::vector<GraphNodeId> inputs;
    std::uint32_t output_physical_slot{kInvalidPhysicalFramebufferSlot};
    std::uint32_t parameter_offset{0};
    std::uint32_t parameter_size{0};
    ::chronon3d::renderer::ProcessorCapabilities capabilities{};
    bool is_fused{false};

    // When non-null, this operation participates in the fully-compiled
    // execute_compiled_program() path and bypasses node.execute().
    CompiledExecuteFn compiled_execute{nullptr};

    [[nodiscard]] bool has_compiled_execute() const noexcept {
        return compiled_execute != nullptr;
    }
};

struct StaticSubgraphBakePass {
    GraphNodeId root_node{k_invalid_node};
    std::uint64_t static_fingerprint{0};
    bool is_baked{false};
    std::uint32_t persistent_surface_handle{0};
};

// ── CompiledLayerInstance ──────────────────────────────────────────────────
//
/// A single layer instance compiled from a fusible chain (Source→…→Composite).
/// Mirror of runtime::LayerInstance, kept here to avoid a circular include
/// (gpu_layer_batch.hpp includes compiled_frame_graph.hpp).
struct CompiledLayerInstance {
    GraphNodeId node{k_invalid_node};  // the source (Image/Text/Rect) node
    std::uint32_t resource_index{0};
    std::uint32_t transform_index{0};
    std::uint32_t paint_index{0};
    float opacity{1.0f};
};

struct CompiledLayerBatch {
    std::vector<GraphNodeId> member_nodes;
    std::vector<CompiledLayerInstance> instances;
    GraphNodeId root_node{k_invalid_node};
    std::uint32_t output_physical_slot{kInvalidPhysicalFramebufferSlot};
    bool is_gpu_fused{false};

    [[nodiscard]] bool has_instances() const noexcept {
        return !instances.empty();
    }
};

struct CompiledFrameProgram {
    // Topological schedule copied once at compile time. Keeping it beside the
    // operations makes the program the executor's immutable source of truth;
    // CompiledFrameGraph::levels remains the compatibility fallback.
    std::vector<std::vector<GraphNodeId>> levels;
    std::vector<CompiledOperation> operations;
    std::vector<StaticSubgraphBakePass> static_bakes;
    std::vector<CompiledLayerBatch> layer_batches;
    bool has_prepared_parameters{false};
    // true when EVERY reachable node has produced a CompiledOperation with
    // a non-null compiled_execute — set by build_compiled_frame_program.
    bool fully_recorded{false};
    bool has_fused_passes{false};
    bool require_native_gpu{false};

    // ── Phase 4 — static bake skip mask ────────────────────────────────
    // Nodes whose output has been pre-baked in prepare().  The executor
    // skips these entirely (execute count = 0).  Populated by merging
    // PreparedFrameProgram::interior_node_skip before the first frame.
    std::vector<bool> interior_node_skip;

    [[nodiscard]] bool empty() const noexcept {
        return operations.empty() || levels.empty();
    }
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
    std::shared_ptr<const ::chronon3d::renderer::ProcessorRegistrySnapshot> processor_snapshot;

    // Immutable handle tables populated once at compile time. Raw processor
    // addresses are never persisted in the compiled graph; they are resolved
    // only at the final backend dispatch boundary through processor_snapshot.
    std::vector<::chronon3d::renderer::ShapeProcessorHandle> shape_processor_table;
    std::vector<::chronon3d::renderer::EffectProcessorHandle> effect_processor_table;

    // Authored-scene topology fingerprint captured by the coordinator when
    // this compiled graph was built. It is compared before refresh so an
    // incorrect scene-structure hint cannot reuse an incompatible graph.
    std::uint64_t authored_structure_fingerprint{0};

    std::vector<std::vector<GraphNodeId>> levels;
    std::vector<std::size_t> consumer_counts;

    std::vector<CompiledNodeInfo> nodes;
    std::vector<ResourceLifetime> lifetimes;

    // Nodes whose transient result is no longer needed after each level.
    // This is derived once from the compiled DAG; execution does not need to
    // rediscover the last consumer by walking graph edges.
    std::vector<std::vector<GraphNodeId>> release_after_level;
    // Generic ownership steals for resources with exactly one consumer.  A
    // processor may use this to write into the producer's physical slot
    // without a domain-specific video/image/text special case.
    std::vector<CompiledOwnershipTransfer> ownership_transfers;

    // Deterministic interval-coloring plan for transient node outputs. The
    // plan is resolution-independent metadata consumed by the executor's
    // existing framebuffer pool; persistent and asynchronous resources are
    // explicitly excluded from aliasing.
    PhysicalFramebufferAllocationPlan physical_framebuffer_plan;
    CompiledFrameProgram program;

    // Optional prepared, domain-neutral per-frame parameter payload.  A
    // missing table is valid: the generic node.execute() fallback remains
    // authoritative for graphs that have not opted into preparation yet.
    std::shared_ptr<const FrameParameterTable> prepared_parameters;

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
