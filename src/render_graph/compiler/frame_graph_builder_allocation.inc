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
    build_compiled_frame_program(compiled);
}

void FrameGraphCompiler::validate_compiled_program_coverage(
    const CompiledFrameGraph& compiled)
{
    std::vector<std::uint32_t> standalone_count(compiled.nodes.size(), 0);
    std::vector<std::uint32_t> fused_count(compiled.nodes.size(), 0);

    for (const auto& operation : compiled.program.operations) {
        if (operation.node >= compiled.nodes.size()) {
            throw std::runtime_error(
                "FrameGraphCompiler: execution owner references invalid node " +
                std::to_string(operation.node));
        }
        if (operation.is_fused) {
            ++fused_count[operation.node];
        } else {
            ++standalone_count[operation.node];
        }
    }

    for (const auto& batch : compiled.program.layer_batches) {
        if (!batch.is_gpu_fused) continue;
        for (const auto node_id : batch.member_nodes) {
            if (node_id >= compiled.nodes.size()) {
                throw std::runtime_error(
                    "FrameGraphCompiler: fused batch references invalid node " +
                    std::to_string(node_id));
            }
            if (std::any_of(compiled.program.operations.begin(),
                            compiled.program.operations.end(),
                            [node_id](const CompiledOperation& operation) {
                                return operation.node == node_id &&
                                       operation.is_fused;
                            })) {
                // The root carries the fused batch operation (counted above);
                // counting it again as a member would claim two owners. Some
                // hand-built validation fixtures omit that root operation,
                // in which case the batch membership is the owner.
                continue;
            }
            ++fused_count[node_id];
        }
    }

    for (GraphNodeId node_id = 0; node_id < compiled.nodes.size(); ++node_id) {
        const auto& node = compiled.nodes[node_id];
        if (!node.reachable) continue;

        const bool standalone = standalone_count[node_id] == 1;
        const bool fused = fused_count[node_id] == 1;
        const bool legally_eliminated =
            node.elimination_reason == EliminationReason::StaticBake ||
            node.elimination_reason == EliminationReason::FusedIntoBatch ||
            node.elimination_reason == EliminationReason::DeadNode ||
            node.elimination_reason == EliminationReason::EarlyExit;
        const int owner_count = static_cast<int>(standalone) +
                                static_cast<int>(fused) +
                                static_cast<int>(legally_eliminated);
        if (owner_count != 1) {
            throw std::runtime_error(
                "FrameGraphCompiler: reachable node lacks exactly one execution "
                "owner or legal elimination reason: node=" +
                std::to_string(node_id) + " name='" + node.name + "'");
        }
    }
}

void FrameGraphCompiler::validate(
    const CompiledFrameGraph& compiled
) const {
    if (compiled.output == k_invalid_node) {
        throw std::runtime_error("FrameGraphCompiler: invalid output node");
    }
}
