#pragma once

#include <chronon3d/internal/render_graph/render_graph.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/render_graph/core/node_identity.hpp>
#include <chronon3d/render_graph/compiler/compiled_resource_table.hpp>
#include <chronon3d/render_graph/pipeline/frame_parameter_table.hpp>
#include <chronon3d/render_graph/pipeline/execution_decision.hpp>
#include <chronon3d/internal/render_graph/processor_registry_snapshot.hpp>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace chronon3d::graph {
using NodeCacheKey = ::chronon3d::cache::NodeCacheKey;

// ── Binding metadata ───────────────────────────────────────────────────
// Attached to CompiledNodeInfo during graph build/compilation.
// The binding compiler reads this to build the binding table.
struct SceneBindingMetadata {
    bool     active{false};
    uint32_t layer_index{0};
    uint32_t item_index{0};
    uint16_t effect_begin{0};
    uint16_t effect_count{0};

    [[nodiscard]] bool has_binding() const { return active; }
};

enum class ExecutionOwner : std::uint8_t {
    None,
    Standalone,
    Fused,
};

enum class EliminationReason : std::uint8_t {
    None,
    StaticBake,
    FusedIntoBatch,
    DeadNode,
    EarlyExit,
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

    int shape_type{-1};
    std::vector<int> source_shape_types;
    std::string processor_id;

    renderer::ShapeProcessorHandle shape_processor{};
    std::uint32_t shape_processors_offset{0};
    std::uint32_t shape_processors_count{0};
    std::uint32_t effect_processors_offset{0};
    std::uint32_t effect_processors_count{0};

    SceneBindingMetadata binding_meta{};

    bool reachable{false};
    bool early_exit_skip{false};
    bool lowered_into_batch{false};
    ExecutionOwner execution_owner{ExecutionOwner::None};
    EliminationReason elimination_reason{EliminationReason::None};

    std::optional<raster::BBox> predicted_bbox;
    StableNodeId stable_node_id{kInvalidStableNodeId};
};

struct CompiledOwnershipTransfer {
    GraphNodeId producer{k_invalid_node};
    GraphNodeId consumer{k_invalid_node};
    bool transferable{false};
};

class RenderBackend;
using CompiledExecuteFn = bool (*)(RenderBackend* backend,
                                   const struct CompiledOperation& op);

struct CompiledOperation {
    GraphNodeId node{k_invalid_node};
    StableNodeId stable_node{kInvalidStableNodeId};
    std::vector<GraphNodeId> inputs;
    std::uint32_t output_physical_slot{kInvalidPhysicalAllocationId};
    std::uint32_t parameter_offset{0};
    std::uint32_t parameter_size{0};
    ::chronon3d::renderer::ProcessorCapabilities capabilities{};
    bool is_fused{false};
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

struct CompiledLayerInstance {
    GraphNodeId node{k_invalid_node};
    std::uint32_t resource_index{0};
    std::uint32_t transform_index{0};
    std::uint32_t paint_index{0};
    float opacity{1.0f};
    raster::BBox dst_bounds{0, 0, 0, 0};
};

struct CompiledLayerBatch {
    std::vector<GraphNodeId> member_nodes;
    std::vector<CompiledLayerInstance> instances;
    GraphNodeId root_node{k_invalid_node};
    std::uint32_t output_physical_slot{kInvalidPhysicalAllocationId};
    bool is_gpu_fused{false};

    [[nodiscard]] bool has_instances() const noexcept {
        return !instances.empty();
    }
};

struct CompiledFrameProgram {
    std::vector<std::vector<GraphNodeId>> levels;
    std::vector<CompiledOperation> operations;
    std::vector<StaticSubgraphBakePass> static_bakes;
    std::vector<CompiledLayerBatch> layer_batches;
    bool has_prepared_parameters{false};
    bool fully_recorded{false};
    bool has_fused_passes{false};
    bool require_native_gpu{false};
    std::vector<bool> interior_node_skip;

    [[nodiscard]] bool empty() const noexcept {
        return operations.empty() || levels.empty();
    }
};

/// Immutable compiled graph plus its canonical compiled resource table.
///
/// CompiledFrameGraph derives from CompiledResourceTable so the table remains
/// one object while legacy direct lifetime/plan spellings continue to resolve
/// to zero-storage aliases owned by the table. There is no parallel physical
/// framebuffer allocation plan or separate lifetime vector.
struct CompiledFrameGraph : CompiledResourceTable {
    RenderGraph graph;
    GraphNodeId output{k_invalid_node};

    std::uint64_t structure_hash{0};

    std::uint64_t registry_generation{0};
    std::uint64_t processor_snapshot_identity{0};
    std::shared_ptr<const ::chronon3d::renderer::ProcessorRegistrySnapshot> processor_snapshot;

    std::vector<::chronon3d::renderer::ShapeProcessorHandle> shape_processor_table;
    std::vector<::chronon3d::renderer::EffectProcessorHandle> effect_processor_table;

    std::uint64_t authored_structure_fingerprint{0};

    std::vector<std::vector<GraphNodeId>> levels;
    std::vector<std::size_t> consumer_counts;

    std::vector<CompiledNodeInfo> nodes;

    // Generic ownership steals for resources with exactly one consumer.
    // This is an execution optimization derived from the canonical resource
    // table, not a second lifetime/allocation authority.
    std::vector<CompiledOwnershipTransfer> ownership_transfers;

    CompiledFrameProgram program;

    std::optional<ExecutionDecision> execution_decision;

    std::shared_ptr<const FrameParameterTable> prepared_parameters;
    std::vector<FrameParameterSlice> parameter_bindings;

    [[nodiscard]] CompiledResourceTable& resource_table() noexcept {
        return *this;
    }

    [[nodiscard]] const CompiledResourceTable& resource_table() const noexcept {
        return *this;
    }

    void set_parameter_bindings(std::vector<FrameParameterSlice> bindings) {
        parameter_bindings = std::move(bindings);
    }

    void apply_parameter_patches(const ParameterPatchSet& patches) {
        if (!prepared_parameters) {
            throw std::logic_error("CompiledFrameGraph has no prepared parameter table");
        }
        auto mutable_table = std::make_shared<FrameParameterTable>(*prepared_parameters);
        for (const auto& patch : patches.patches) {
            mutable_table->patch(patch.offset, patch.value);
        }
        prepared_parameters = std::move(mutable_table);
    }

    std::vector<bool> early_exit_skip;
    bool skip_initial_clear{false};

    bool valid{false};

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
