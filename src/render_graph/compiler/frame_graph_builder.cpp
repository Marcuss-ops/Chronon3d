// ═══════════════════════════════════════════════════════════════════════════
// frame_graph_builder.cpp — extracted private builders (FASE 16)
// ═══════════════════════════════════════════════════════════════════════════
//
// Member function definitions extracted from frame_graph_compiler.cpp.
// Kept here to make frame_graph_compiler.cpp focused on the public
// compile() / compile_with_reuse() / compute_structure_hash() surface.
//
// Builds:
//   - build_execution_levels()      — topological sort, levels, consumer_counts
//   - build_node_metadata()         — per-node fields, Merkle stable_node_id
//   - compute_resource_lifetimes()  — first_level/last_level per resource
//   - validate()                    — output node validity check

#include <chronon3d/render_graph/compiler/frame_graph_compiler.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/render_graph/nodes/source_node.hpp>
#include <chronon3d/render_graph/nodes/multi_source_node.hpp>
#include <chronon3d/render_graph/nodes/text_run_node.hpp>
#include <chronon3d/render_graph/nodes/effect_stack_node.hpp>
#include <chronon3d/render_graph/nodes/adjustment_node.hpp>
#include <chronon3d/render_graph/nodes/dof_node.hpp>

#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <typeindex>

namespace chronon3d::graph {

void FrameGraphCompiler::build_execution_levels(
    RenderGraph& graph,
    GraphNodeId output,
    CompiledFrameGraph& compiled
) const {
    const size_t node_count = graph.size();
    std::vector<char> reachable(node_count, 0);
    std::vector<GraphNodeId> stack{output};
    while (!stack.empty()) {
        GraphNodeId id = stack.back();
        stack.pop_back();
        if (id >= node_count || reachable[id]) {
            continue;
        }
        reachable[id] = 1;
        for (GraphNodeId parent : graph.inputs(id)) {
            stack.push_back(parent);
        }
    }

    std::vector<std::vector<GraphNodeId>> children(node_count);
    std::vector<size_t> indegree(node_count, 0);
    compiled.consumer_counts.assign(node_count, 0);

    for (GraphNodeId child = 0; child < node_count; ++child) {
        if (!reachable[child]) {
            continue;
        }
        for (GraphNodeId parent : graph.inputs(child)) {
            if (!reachable[parent]) {
                continue;
            }
            children[parent].push_back(child);
            ++indegree[child];
            ++compiled.consumer_counts[parent];
        }
    }

    std::vector<GraphNodeId> current_level;
    current_level.reserve(node_count);
    for (GraphNodeId id = 0; id < node_count; ++id) {
        if (reachable[id] && indegree[id] == 0) {
            current_level.push_back(id);
        }
    }

    size_t scheduled = 0;
    while (!current_level.empty()) {
        compiled.levels.push_back(current_level);
        scheduled += current_level.size();

        std::vector<GraphNodeId> next_level;
        for (GraphNodeId id : current_level) {
            for (GraphNodeId child : children[id]) {
                if (--indegree[child] == 0) {
                    next_level.push_back(child);
                }
            }
        }
        current_level.swap(next_level);
    }

    const size_t reachable_count = static_cast<size_t>(
        std::count(reachable.begin(), reachable.end(), static_cast<char>(1))
    );
    if (scheduled != reachable_count) {
        throw std::runtime_error("FrameGraphCompiler: graph is not a DAG or contains unreachable dependency cycles");
    }
}

void FrameGraphCompiler::build_node_metadata(
    RenderGraph& graph,
    RenderGraphContext& ctx,
    CompiledFrameGraph& compiled,
    const FrameGraphCompileOptions& options
) const {
    const size_t node_count = graph.size();
    compiled.nodes.resize(node_count);

    // Capture processor ownership once at the compiler boundary. Real
    // SoftwareBackend instances provide an immutable owning snapshot. A
    // backend that participates in shape/effect compilation without one is
    // rejected at the first binding request; non-renderable graph operators
    // remain compilable without a backend.
    compiled.processor_snapshot = ctx.services.backend
        ? ctx.services.backend->processor_snapshot()
        : nullptr;
    // Standalone graph callers may provide a backend without going through
    // scene_context_setup(), leaving the optional context generation at its
    // zero sentinel. In that case the owning backend snapshot is the
    // canonical registry generation; use it rather than rejecting a valid
    // graph at the compiler boundary.
    if (ctx.services.registry_generation == 0 && compiled.processor_snapshot) {
        ctx.services.registry_generation = compiled.processor_snapshot->generation();
    }
    compiled.registry_generation = ctx.services.registry_generation;
    compiled.processor_snapshot_identity = compiled.processor_snapshot
        ? compiled.processor_snapshot->identity()
        : 0;
    if (compiled.processor_snapshot &&
        compiled.processor_snapshot->generation() != compiled.registry_generation) {
        throw std::runtime_error(
            "FrameGraphCompiler: processor snapshot generation does not match "
            "RenderGraphContext registry generation");
    }

    const auto require_snapshot = [&]() -> const renderer::ProcessorRegistrySnapshot& {
        if (!compiled.processor_snapshot) {
            throw std::runtime_error(
                "FrameGraphCompiler: backend must provide an owning processor snapshot");
        }
        return *compiled.processor_snapshot;
    };
    const bool requires_snapshot = ctx.services.backend &&
        ctx.services.backend->requires_processor_snapshot();
    const auto resolve_shape = [&](ShapeType type) {
        return require_snapshot().shape_handle(type);
    };
    const auto resolve_effect = [&](std::type_index type) {
        return require_snapshot().effect_handle(type);
    };
    std::vector<char> reachable(node_count, 0);
    for (const auto& level : compiled.levels) {
        for (GraphNodeId id : level) {
            reachable[id] = 1;
        }
    }

    std::vector<std::vector<GraphNodeId>> children(node_count);
    for (GraphNodeId child = 0; child < node_count; ++child) {
        if (!reachable[child]) continue;
        for (GraphNodeId parent : graph.inputs(child)) {
            if (!reachable[parent]) continue;
            children[parent].push_back(child);
        }
    }

    for (GraphNodeId id = 0; id < node_count; ++id) {
        auto& node_info = compiled.nodes[id];
        node_info.id = id;
        if (graph.has_node(id)) {
            auto& node = graph.node(id);
            node_info.name = node.name();
            node_info.layer_id = node.layer_id();
            node_info.kind = node.kind();
            node_info.inputs = graph.inputs(id);
            if (reachable[id]) {
                node_info.reachable = true;
                node_info.consumers = children[id];

                // Carry the builder's canonical layer location into the
                // compiled program.  The binding table used to depend on
                // metadata that no production builder ever populated, so
                // every real precomp program ended up with zero bindings and
                // its per-frame refresh path was silently skipped.
                const bool refreshable_kind =
                    node_info.kind == RenderGraphNodeKind::Source ||
                    node_info.kind == RenderGraphNodeKind::TextRun ||
                    node_info.kind == RenderGraphNodeKind::Transform ||
                    node_info.kind == RenderGraphNodeKind::Effect;
                const bool has_layer_location = node.layer_index() != UINT32_MAX;
                const bool is_root_source =
                    !has_layer_location && node.layer_id().empty() &&
                    (node_info.kind == RenderGraphNodeKind::Source ||
                     node_info.kind == RenderGraphNodeKind::TextRun);
                if (refreshable_kind && (has_layer_location || is_root_source)) {
                    node_info.binding_meta = SceneBindingMetadata{
                        true,
                        node.layer_index(),
                        node.item_index(),
                        0,
                        0,
                    };
                }

                node_info.cache_policy = node.cache_policy();

                // Capture structural payload discriminators once at compile
                // time. Refresh may replace dynamic content, matrices and
                // cache keys, but it must never change a node's render kind
                // or shape topology in place.
                if (const auto* source = dynamic_cast<const SourceNode*>(&node)) {
                    node_info.processor_id = "source:" +
                        std::to_string(static_cast<int>(source->render_node().shape.type()));
                    node_info.shape_type = static_cast<int>(source->render_node().shape.type());
                    node_info.shape_processor = requires_snapshot
                        ? resolve_shape(source->render_node().shape.type())
                        : renderer::ShapeProcessorHandle{};
                    if (requires_snapshot && !node_info.shape_processor.valid()) {
                        throw std::runtime_error(
                            "FrameGraphCompiler: missing compiled shape processor for node '" +
                            std::string(node.name()) + "'");
                    }
                    node_info.shape_processors_offset = static_cast<std::uint32_t>(
                        compiled.shape_processor_table.size());
                    node_info.shape_processors_count = 1;
                    compiled.shape_processor_table.push_back(node_info.shape_processor);
                } else if (const auto* multi = dynamic_cast<const MultiSourceNode*>(&node)) {
                    node_info.processor_id = "multi_source";
                    node_info.shape_type = -2;
                    node_info.source_shape_types.reserve(multi->items().size());
                    node_info.shape_processors_offset = static_cast<std::uint32_t>(
                        compiled.shape_processor_table.size());
                    node_info.shape_processors_count = static_cast<std::uint32_t>(
                        multi->items().size());
                    for (const auto& item : multi->items()) {
                        if (!item.node) {
                            throw std::runtime_error(
                                "FrameGraphCompiler: multi-source node '" +
                                std::string(node.name()) +
                                "' contains a null renderable item");
                        }
                        node_info.source_shape_types.push_back(
                            static_cast<int>(item.node->shape.type()));
                        if (item.node->shape.type() == ShapeType::TextRun) {
                            compiled.shape_processor_table.push_back(renderer::ShapeProcessorHandle{});
                        } else {
                            const auto handle = requires_snapshot
                                ? resolve_shape(item.node->shape.type())
                                : renderer::ShapeProcessorHandle{};
                            if (requires_snapshot && !handle.valid()) {
                                throw std::runtime_error(
                                    "FrameGraphCompiler: missing compiled shape processor for multi-source node '" +
                                    std::string(node.name()) + "'");
                            }
                            compiled.shape_processor_table.push_back(handle);
                        }
                    }
                } else if (const auto* text = dynamic_cast<const TextRunNode*>(&node)) {
                    node_info.processor_id = "text_run";
                    node_info.shape_type = static_cast<int>(text->render_node().shape.type());
                } else if (const auto* effect = dynamic_cast<const EffectStackNode*>(&node)) {
                    node_info.processor_id = "effect_stack";
                    node_info.effect_processors_offset = static_cast<std::uint32_t>(
                        compiled.effect_processor_table.size());
                    node_info.effect_processors_count = static_cast<std::uint32_t>(
                        effect->effects().size());
                    for (const auto& instance : effect->effects()) {
                        const auto handle = instance.enabled && requires_snapshot
                            ? resolve_effect(instance.param_type_index())
                            : renderer::EffectProcessorHandle{};
                        if (instance.enabled && requires_snapshot && !handle.valid()) {
                            throw std::runtime_error(
                                "FrameGraphCompiler: missing compiled effect processor for node '" +
                                std::string(node.name()) + "'");
                        }
                        compiled.effect_processor_table.push_back(handle);
                    }
                } else if (const auto* adjustment = dynamic_cast<const AdjustmentNode*>(&node)) {
                    node_info.processor_id = "adjustment";
                    node_info.effect_processors_offset = static_cast<std::uint32_t>(
                        compiled.effect_processor_table.size());
                    node_info.effect_processors_count = static_cast<std::uint32_t>(
                        adjustment->effects().size());
                    for (const auto& instance : adjustment->effects()) {
                        const auto handle = instance.enabled && requires_snapshot
                            ? resolve_effect(instance.param_type_index())
                            : renderer::EffectProcessorHandle{};
                        if (instance.enabled && requires_snapshot && !handle.valid()) {
                            throw std::runtime_error(
                                "FrameGraphCompiler: missing compiled effect processor for node '" +
                                std::string(node.name()) + "'");
                        }
                        compiled.effect_processor_table.push_back(handle);
                    }
                } else if (dynamic_cast<const DofEffectNode*>(&node)) {
                    node_info.processor_id = "dof";
                    const auto handle = requires_snapshot
                        ? resolve_effect(std::type_index(typeid(BlurParams)))
                        : renderer::EffectProcessorHandle{};
                    if (requires_snapshot && !handle.valid()) {
                        throw std::runtime_error(
                            "FrameGraphCompiler: missing compiled effect processor for node '" +
                            std::string(node.name()) + "'");
                    }
                    node_info.effect_processors_offset = static_cast<std::uint32_t>(
                        compiled.effect_processor_table.size());
                    node_info.effect_processors_count = 1;
                    compiled.effect_processor_table.push_back(handle);
                } else {
                    node_info.processor_id = std::string(to_string(node_info.kind));
                }

                if (id < ctx.node_exec.early_exit_skip.size() && ctx.node_exec.early_exit_skip[id]) {
                    node_info.early_exit_skip = true;
                }
                if (options.compute_bboxes) {
                    node_info.predicted_bbox = node.predicted_bbox(ctx);
                }

                // ── Work Package 4 — derive stable_node_id ──
                const std::uint64_t layer_id_hash =
                    hash_string(node_info.layer_id);
                const std::uint64_t kind_and_name_hash = hash_combine(
                    static_cast<std::uint64_t>(node_info.kind),
                    hash_string(node_info.name)
                );
                node_info.stable_node_id = hash_stable_node_inputs(
                    layer_id_hash, kind_and_name_hash);
            }
        }
    }

    // ── Work Package 4.4 — input-aware refinement (Merkle-style) ────────
    // Resolve from sinks toward sources so consumer identities already carry
    // their own downstream context when they are folded into a producer.
    for (auto level_it = compiled.levels.rbegin();
         level_it != compiled.levels.rend(); ++level_it) {
        for (GraphNodeId id : *level_it) {
            if (id >= node_count) continue;
            auto& node_info = compiled.nodes[id];
            if (!node_info.reachable) continue;
            if (node_info.stable_node_id == kInvalidStableNodeId) continue;

            uint64_t h = node_info.stable_node_id.value;
            bool folded = false;

            std::vector<uint64_t> input_sids;
            input_sids.reserve(node_info.inputs.size());
            for (GraphNodeId input_id : node_info.inputs) {
                if (input_id >= node_count) continue;
                const auto& input_info = compiled.nodes[input_id];
                if (!input_info.reachable) continue;
                if (input_info.stable_node_id == kInvalidStableNodeId) continue;
                input_sids.push_back(input_info.stable_node_id.value);
            }
            std::sort(input_sids.begin(), input_sids.end());
            // Include both sides of the local graph neighbourhood.  A source
            // node can be materialised more than once for a derived pass
            // (for example, a shadow caster): those instances may have the
            // same layer/name/kind and no inputs, but they feed different
            // consumers.  Folding sorted consumer identities keeps the ID
            // content-derived without using the volatile graph-node index.
            std::vector<uint64_t> consumer_sids;
            consumer_sids.reserve(children[id].size());
            for (GraphNodeId child_id : children[id]) {
                if (child_id >= node_count) continue;
                const auto& child_info = compiled.nodes[child_id];
                if (!child_info.reachable ||
                    child_info.stable_node_id == kInvalidStableNodeId) {
                    continue;
                }
                consumer_sids.push_back(child_info.stable_node_id.value);
            }
            std::sort(consumer_sids.begin(), consumer_sids.end());

            constexpr uint64_t kInputDomain = 0x494e505554ULL;    // INPUT
            constexpr uint64_t kConsumerDomain = 0x434f4e53554d4552ULL; // CONSUMER
            h ^= kInputDomain;
            h *= 0x100000001b3ULL;
            h ^= static_cast<uint64_t>(input_sids.size());
            h *= 0x100000001b3ULL;
            for (uint64_t sid : input_sids) {
                h ^= sid;
                h *= 0x100000001b3ULL;
                folded = true;
            }
            h ^= kConsumerDomain;
            h *= 0x100000001b3ULL;
            h ^= static_cast<uint64_t>(consumer_sids.size());
            h *= 0x100000001b3ULL;
            for (uint64_t sid : consumer_sids) {
                h ^= sid;
                h *= 0x100000001b3ULL;
                folded = true;
            }
            if (folded) {
                node_info.stable_node_id = StableNodeId{h == 0u ? 1u : h};
            }
        }
    }

    // ── Work Package 4 — collision detection ─────────────────────────────
    {
        std::unordered_map<StableNodeId, GraphNodeId> seen;
        for (size_t i = 0; i < compiled.nodes.size(); ++i) {
            if (!compiled.nodes[i].reachable) continue;
            const auto sid = compiled.nodes[i].stable_node_id;
            if (sid == kInvalidStableNodeId) continue;
            auto [it, inserted] = seen.emplace(sid, static_cast<GraphNodeId>(i));
            if (!inserted) {
                const auto describe_consumers = [&](GraphNodeId node_id) {
                    std::string text;
                    for (GraphNodeId child_id : compiled.nodes[node_id].consumers) {
                        if (!text.empty()) text += ",";
                        text += std::to_string(child_id);
                        if (child_id < compiled.nodes.size()) {
                            text += ":" + compiled.nodes[child_id].name;
                        }
                    }
                    return text.empty() ? std::string{"-"} : text;
                };
                throw std::runtime_error(
                    "FrameGraphCompiler: stable_node_id collision between nodes "
                    + std::to_string(it->second) + " (layer='"
                    + compiled.nodes[it->second].layer_id + "', name='"
                    + compiled.nodes[it->second].name + "', kind="
                    + std::to_string(static_cast<int>(compiled.nodes[it->second].kind))
                    + ") and " + std::to_string(i) + " (layer='"
                    + compiled.nodes[i].layer_id + "', name='"
                    + compiled.nodes[i].name + "', kind="
                    + std::to_string(static_cast<int>(compiled.nodes[i].kind))
                    + ", consumers=" + describe_consumers(i) + ")"
                    + "; first_consumers=" + describe_consumers(it->second));
            }
        }
    }
}

namespace {

// The graph taxonomy deliberately keeps non-shape nodes out of the shape
// processor boundary. Null/Group/Control are not RenderGraphNodeKind values
// in this repository; TextRun has its own processor path, Image is an
// ordinary renderable ShapeType, and Transition is a graph node that operates
// on framebuffer inputs. Only SourceNode/MultiSourceNode payloads enter this
// validator, so a non-shape node can never be rejected as ShapeType::None.
[[nodiscard]] bool uses_shape_processor(const RenderGraphNode& node) noexcept {
    return dynamic_cast<const SourceNode*>(&node) != nullptr ||
           dynamic_cast<const MultiSourceNode*>(&node) != nullptr;
}

} // namespace

void FrameGraphCompiler::validate_renderable_shape(
    const ::chronon3d::RenderNode& render_node,
    const CompiledNodeInfo& node_info,
    const RenderGraphContext& ctx
) const {
    const auto shape_type = render_node.shape.type();
    if (shape_type == ShapeType::None) {
        throw std::runtime_error(
            "FrameGraphCompiler: renderable Shape node '" +
            node_info.name + "' has ShapeType::None and cannot reach a shape processor");
    }

    if (!ctx.services.backend) {
        throw std::runtime_error(
            "FrameGraphCompiler: renderable Shape node '" +
            node_info.name + "' has no render backend");
    }
    if (const auto error = ctx.services.backend->validate_render_node(render_node)) {
        throw std::runtime_error(
            "FrameGraphCompiler: renderable Shape node '" +
            node_info.name + "' is invalid: " + error->message);
    }
}

void FrameGraphCompiler::validate_renderable_graph(
    const RenderGraph& graph,
    GraphNodeId output,
    const RenderGraphContext& ctx
) const {
    const size_t node_count = graph.size();
    std::vector<char> reachable(node_count, 0);
    std::vector<GraphNodeId> stack{output};
    while (!stack.empty()) {
        const GraphNodeId id = stack.back();
        stack.pop_back();
        if (id >= node_count || reachable[id]) {
            continue;
        }
        reachable[id] = 1;
        for (const GraphNodeId parent : graph.inputs(id)) {
            stack.push_back(parent);
        }
    }

    for (GraphNodeId id = 0; id < node_count; ++id) {
        if (!reachable[id] || !graph.has_node(id)) {
            continue;
        }
        const auto& node = graph.node(id);
        if (!uses_shape_processor(node)) {
            // TextRunNode, TransitionNode, and the remaining graph operators
            // have dedicated processor/input paths. They must not be forced
            // through ShapeType validation; Null/Group/Control are likewise
            // absent from this graph taxonomy rather than represented by a
            // fake ShapeType::None placeholder.
            continue;
        }
        if (const auto* source = dynamic_cast<const SourceNode*>(&node)) {
            CompiledNodeInfo info;
            info.id = id;
            info.name = node.name();
            validate_renderable_shape(source->render_node(), info, ctx);
        } else if (const auto* multi = dynamic_cast<const MultiSourceNode*>(&node)) {
            CompiledNodeInfo info;
            info.id = id;
            info.name = node.name();
            for (const auto& item : multi->items()) {
                if (!item.node) {
                    throw std::runtime_error(
                        "FrameGraphCompiler: multi-source node '" +
                        std::string(node.name()) +
                        "' contains a null renderable item");
                }
                validate_renderable_shape(*item.node, info, ctx);
            }
        }
    }
}

void FrameGraphCompiler::compute_resource_lifetimes(
    CompiledFrameGraph& compiled
) const {
    const size_t node_count = compiled.graph.size();
    compiled.lifetimes.assign(node_count, ResourceLifetime{});

    for (size_t level_index = 0; level_index < compiled.levels.size(); ++level_index) {
        for (GraphNodeId node_id : compiled.levels[level_index]) {
            if (node_id < node_count) {
                compiled.lifetimes[node_id].producer = node_id;
                compiled.lifetimes[node_id].first_level = level_index;
                compiled.lifetimes[node_id].last_level = level_index;
            }
        }
    }

    for (size_t level_index = 0; level_index < compiled.levels.size(); ++level_index) {
        for (GraphNodeId node_id : compiled.levels[level_index]) {
            if (node_id >= node_count) continue;
            for (GraphNodeId input_id : compiled.nodes[node_id].inputs) {
                if (input_id < node_count) {
                    compiled.lifetimes[input_id].last_level = std::max(
                        compiled.lifetimes[input_id].last_level, level_index
                    );
                    compiled.lifetimes[input_id].consumer_count++;
                }
            }
        }
    }
}

void FrameGraphCompiler::build_physical_framebuffer_allocation_plan(
    CompiledFrameGraph& compiled
) const {
    auto& plan = compiled.physical_framebuffer_plan;
    plan = PhysicalFramebufferAllocationPlan{};
    plan.resources.resize(compiled.graph.size());

    struct Interval {
        GraphNodeId producer{k_invalid_node};
        std::size_t first_level{0};
        std::size_t last_level{0};
    };

    std::vector<Interval> intervals;
    intervals.reserve(compiled.nodes.size());
    std::vector<std::size_t> level_live_counts(compiled.levels.size(), 0);
    for (GraphNodeId id = 0; id < compiled.graph.size(); ++id) {
        if (id >= compiled.nodes.size() || !compiled.nodes[id].reachable ||
            id >= compiled.lifetimes.size()) {
            continue;
        }

        const auto& lifetime = compiled.lifetimes[id];
        auto& allocation = plan.resources[id];
        ++plan.logical_resource_count;
        allocation.producer = id;
        allocation.persistent =
            compiled.nodes[id].cache_policy.reusable_across_frames() ||
            id == compiled.output;
        allocation.async_use = !lifetime.can_release_after_last_consumer;

        // A frame-invariant cache entry (or the final graph output) may still
        // be referenced after this execution state is destroyed. It must keep
        // its normal pool/shared ownership and cannot borrow a slot owned by
        // this frame. The final output is excluded for the same reason: the
        // executor returns it after ExecutionState teardown.
        if (allocation.persistent) {
            ++plan.excluded_persistent_count;
            continue;
        }
        if (allocation.async_use) {
            ++plan.excluded_async_count;
            continue;
        }
        allocation.aliasable = true;
        ++plan.aliasable_resource_count;
        intervals.push_back(Interval{id, lifetime.first_level, lifetime.last_level});
        if (!level_live_counts.empty() &&
            lifetime.first_level < level_live_counts.size()) {
            const auto last = std::min(
                lifetime.last_level, level_live_counts.size() - 1);
            for (std::size_t level = lifetime.first_level; level <= last; ++level) {
                ++level_live_counts[level];
            }
        }
    }

    // Stable first-fit interval coloring: level, then producer id. The strict
    // `<` test is intentional because two resources alive in the same level
    // must never share storage, even when their node execution happens to be
    // serialized by the current scheduler.
    std::sort(intervals.begin(), intervals.end(), [](const Interval& lhs, const Interval& rhs) {
        if (lhs.first_level != rhs.first_level) {
            return lhs.first_level < rhs.first_level;
        }
        return lhs.producer < rhs.producer;
    });

    std::vector<std::size_t> slot_last_level;
    for (const auto& interval : intervals) {
        std::uint32_t selected = kInvalidPhysicalFramebufferSlot;
        for (std::uint32_t slot = 0; slot < slot_last_level.size(); ++slot) {
            if (slot_last_level[slot] < interval.first_level) {
                selected = slot;
                break;
            }
        }
        if (selected == kInvalidPhysicalFramebufferSlot) {
            selected = static_cast<std::uint32_t>(slot_last_level.size());
            slot_last_level.push_back(interval.last_level);
            plan.slots.push_back(FramebufferSlot{selected, 0, 0});
        } else {
            slot_last_level[selected] = interval.last_level;
        }
        plan.resources[interval.producer].physical_slot = selected;
    }
    plan.physical_slot_count = static_cast<std::uint32_t>(plan.slots.size());
    for (const auto count : level_live_counts) {
        plan.peak_live_resource_count = std::max(
            plan.peak_live_resource_count,
            static_cast<std::uint32_t>(count));
    }
}

void FrameGraphCompiler::validate(
    const CompiledFrameGraph& compiled
) const {
    if (compiled.output == k_invalid_node) {
        throw std::runtime_error("FrameGraphCompiler: invalid output node");
    }
}

} // namespace chronon3d::graph
