void FrameGraphCompiler::build_compiled_resource_table(
    CompiledFrameGraph& compiled
) const {
    auto& table = compiled.resource_table();
    table.clear();

    const std::size_t node_count = compiled.graph.size();
    table.resources.resize(node_count);
    table.release_after_level.assign(compiled.levels.size(), {});
    compiled.ownership_transfers.assign(node_count, CompiledOwnershipTransfer{});

    // First pass: establish the producer level and canonical descriptor for
    // every reachable logical framebuffer result.
    for (std::size_t level_index = 0; level_index < compiled.levels.size(); ++level_index) {
        for (GraphNodeId node_id : compiled.levels[level_index]) {
            if (node_id >= node_count || node_id >= compiled.nodes.size() ||
                !compiled.nodes[node_id].reachable) {
                continue;
            }

            auto& resource = table.resources[node_id];
            resource.producer = node_id;
            resource.first_level = level_index;
            resource.last_level = level_index;
            resource.desc.kind = runtime::ResourceKind::Color;
            resource.desc.usage = runtime::ResourceUsage::ColorAttachment;
            resource.desc.lifetime = runtime::LifetimeClass::FrameTransient;
        }
    }

    // Second pass: fold every consumer edge into the same canonical record.
    // No separate lifetime vector is built or reconciled later.
    for (std::size_t level_index = 0; level_index < compiled.levels.size(); ++level_index) {
        for (GraphNodeId node_id : compiled.levels[level_index]) {
            if (node_id >= compiled.nodes.size()) continue;
            for (GraphNodeId input_id : compiled.nodes[node_id].inputs) {
                auto* input = table.resource_for(input_id);
                if (!input) continue;
                input->last_level = std::max(input->last_level, level_index);
                ++input->consumer_count;
            }
        }
    }

    runtime::ResourcePlanner planner;
    std::vector<GraphNodeId> planner_request_to_resource;
    planner_request_to_resource.reserve(node_count);
    std::vector<std::size_t> level_live_counts(compiled.levels.size(), 0);

    for (GraphNodeId node_id = 0; node_id < node_count; ++node_id) {
        auto* resource = table.resource_for(node_id);
        if (!resource) continue;

        ++table.logical_resource_count;
        resource->persistent =
            compiled.nodes[node_id].cache_policy.reusable_across_frames() ||
            node_id == compiled.output;
        resource->async_use = !resource->can_release_after_last_consumer;

        if (resource->persistent) {
            resource->desc.lifetime = runtime::LifetimeClass::JobPersistent;
            ++table.excluded_persistent_count;
        } else if (resource->async_use) {
            resource->desc.lifetime = runtime::LifetimeClass::PipelineSlot;
            ++table.excluded_async_count;
        } else {
            resource->desc.lifetime = runtime::LifetimeClass::FrameTransient;
            resource->aliasable = true;
            ++table.aliasable_resource_count;

            planner.add(runtime::ResourceRequest{
                std::to_string(node_id),
                resource->desc,
                resource->first_level,
                resource->last_level});
            planner_request_to_resource.push_back(node_id);

            if (!level_live_counts.empty() &&
                resource->first_level < level_live_counts.size()) {
                const auto last = std::min(
                    resource->last_level, level_live_counts.size() - 1);
                for (std::size_t level = resource->first_level; level <= last; ++level) {
                    ++level_live_counts[level];
                }
            }
        }

        // The graph output remains owned by the executor until return. Nodes
        // with no consumers likewise have no release event inside the graph.
        if (node_id != compiled.output && resource->consumer_count != 0 &&
            resource->last_level < table.release_after_level.size()) {
            table.release_after_level[resource->last_level].push_back(node_id);
        }

        if (resource->consumer_count == 1 &&
            compiled.nodes[node_id].consumers.size() == 1) {
            compiled.ownership_transfers[node_id] = CompiledOwnershipTransfer{
                node_id, compiled.nodes[node_id].consumers.front(), true};
        }
    }

    // ResourcePlanner is the sole allocation algorithm. It is intentionally
    // ephemeral here: only the canonical physical slot metadata and logical
    // -> physical ids are persisted in CompiledResourceTable.
    const auto plan = planner.build();
    if (plan.slots.size() >= static_cast<std::size_t>(kInvalidPhysicalAllocationId)) {
        throw std::runtime_error(
            "FrameGraphCompiler: compiled resource table exceeds physical allocation id range");
    }
    table.slots = plan.slots;
    table.physical_slot_count = static_cast<std::uint32_t>(table.slots.size());

    for (std::size_t request_index = 0;
         request_index < planner_request_to_resource.size();
         ++request_index) {
        const auto* allocation = plan.allocation_for(request_index);
        if (!allocation ||
            allocation->physical_slot == std::numeric_limits<std::size_t>::max()) {
            continue;
        }
        if (allocation->physical_slot >=
            static_cast<std::size_t>(kInvalidPhysicalAllocationId)) {
            throw std::runtime_error(
                "FrameGraphCompiler: physical allocation id cannot be represented");
        }
        auto* resource = table.resource_for(planner_request_to_resource[request_index]);
        if (resource) {
            resource->physical_slot =
                static_cast<PhysicalAllocationId>(allocation->physical_slot);
        }
    }

    for (const auto count : level_live_counts) {
        table.peak_live_resource_count = std::max(
            table.peak_live_resource_count,
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
