#pragma once

#include <chronon3d/internal/render_graph/render_graph.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/media/render_to_media.hpp>
#include <chronon3d/render_graph/core/node_identity.hpp>
#include <chronon3d/render_graph/compiler/compiled_resource_table.hpp>
#include <chronon3d/render_graph/pipeline/frame_parameter_table.hpp>
#include <chronon3d/render_graph/pipeline/execution_decision.hpp>
#include <chronon3d/internal/render_graph/processor_registry_snapshot.hpp>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace chronon3d::graph {
using NodeCacheKey = ::chronon3d::cache::NodeCacheKey;

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

class RenderBackend;
using CompiledExecuteFn = bool (*)(RenderBackend* backend,
                                   const struct CompiledOperation& op);

inline constexpr std::uint32_t kInvalidPassTiming =
    std::numeric_limits<std::uint32_t>::max();

/// One compiled pass owns exactly one pair of timestamp queries. Backends fill
/// gpu_duration_ns after resolving the pair; frame GPU time is just the sum of
/// resolved pass durations and requires no second timing model.
struct PassTiming {
    std::uint32_t begin_query{0};
    std::uint32_t end_query{0};
    std::uint64_t gpu_duration_ns{0};
    bool resolved{false};
};

struct PassQueryArena {
    std::vector<PassTiming> timings;
    std::uint32_t next_query{0};

    void clear() noexcept {
        timings.clear();
        next_query = 0;
    }

    [[nodiscard]] std::uint32_t allocate() {
        const auto index = static_cast<std::uint32_t>(timings.size());
        const auto begin = next_query++;
        const auto end = next_query++;
        timings.push_back(PassTiming{begin, end, 0, false});
        return index;
    }

    [[nodiscard]] PassTiming* timing(std::uint32_t index) noexcept {
        return index < timings.size() ? &timings[index] : nullptr;
    }

    [[nodiscard]] const PassTiming* timing(std::uint32_t index) const noexcept {
        return index < timings.size() ? &timings[index] : nullptr;
    }

    [[nodiscard]] std::uint64_t gpu_frame_time_ns() const noexcept {
        std::uint64_t total = 0;
        for (const auto& timing : timings) {
            if (timing.resolved) total += timing.gpu_duration_ns;
        }
        return total;
    }
};

struct CompiledOperation {
    GraphNodeId node{k_invalid_node};
    StableNodeId stable_node{kInvalidStableNodeId};
    std::vector<GraphNodeId> inputs;
    std::uint32_t output_physical_slot{kInvalidPhysicalAllocationId};
    std::uint32_t parameter_offset{0};
    std::uint32_t parameter_size{0};
    std::uint32_t pass_timing{kInvalidPassTiming};
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
    PassQueryArena query_arena;
    bool has_prepared_parameters{false};
    bool fully_recorded{false};
    bool has_fused_passes{false};
    bool require_native_gpu{false};
    std::vector<bool> interior_node_skip;

    void allocate_pass_queries() {
        query_arena.clear();
        for (auto& operation : operations) {
            operation.pass_timing = query_arena.allocate();
        }
    }

    [[nodiscard]] std::uint64_t gpu_frame_time_ns() const noexcept {
        return query_arena.gpu_frame_time_ns();
    }

    [[nodiscard]] bool empty() const noexcept {
        return operations.empty() || levels.empty();
    }
};

/// Immutable compiled graph plus its sole persisted resource authority.
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

    std::vector<CompiledNodeInfo> nodes;

    CompiledFrameProgram program;

    std::optional<ExecutionDecision> execution_decision;
    std::optional<::chronon3d::media::RenderToMediaPlan> render_to_media;

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
