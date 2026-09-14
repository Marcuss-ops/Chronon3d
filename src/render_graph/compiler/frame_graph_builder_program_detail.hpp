namespace {

CompiledOperation make_base_operation(
    CompiledFrameGraph& compiled,
    GraphNodeId node_id) {
    CompiledOperation operation;
    operation.node = node_id;
    operation.stable_node = compiled.nodes[node_id].stable_node_id;
    operation.inputs = compiled.nodes[node_id].inputs;
    if (const auto* resource = compiled.resource_table().resource_for(node_id)) {
        operation.output_physical_slot = resource->physical_slot;
    }
    return operation;
}

void resolve_operation_recorder(
    CompiledFrameGraph& compiled,
    const CompiledNodeInfo& node_info,
    CompiledOperation& operation) {
    if (!compiled.processor_snapshot) return;
    if (node_info.shape_processor.valid()) {
        auto shape = compiled.processor_snapshot->shape_shared(node_info.shape_processor);
        if (shape) {
            auto recorder = shape->compile_recorder();
            if (recorder) {
                renderer::CompileNodeContext ctx;
                ctx.node_info = &node_info;
                ctx.input_ids = node_info.inputs.empty() ? nullptr : node_info.inputs.data();
                ctx.input_count = static_cast<std::uint32_t>(node_info.inputs.size());
                ctx.output_physical_slot = operation.output_physical_slot;
                operation = recorder(ctx);
                operation.node = node_info.id;
                operation.stable_node = node_info.stable_node_id;
                operation.inputs = node_info.inputs;
            }
        }
    }
    if (!operation.has_compiled_execute() && node_info.effect_processors_count > 0) {
        const auto handle_index = node_info.effect_processors_offset;
        if (handle_index < compiled.effect_processor_table.size()) {
            auto effect = compiled.processor_snapshot->effect_shared(
                compiled.effect_processor_table[handle_index]);
            if (effect) {
                auto recorder = effect->compile_recorder();
                if (recorder) {
                    renderer::CompileNodeContext ctx;
                    ctx.node_info = &node_info;
                    ctx.input_ids = node_info.inputs.empty() ? nullptr : node_info.inputs.data();
                    ctx.input_count = static_cast<std::uint32_t>(node_info.inputs.size());
                    ctx.output_physical_slot = operation.output_physical_slot;
                    operation = recorder(ctx);
                    operation.node = node_info.id;
                    operation.stable_node = node_info.stable_node_id;
                    operation.inputs = node_info.inputs;
                }
            }
        }
    }
}

bool is_fusible_layer_node(
    CompiledFrameGraph& compiled,
    GraphNodeId node_id,
    const CompiledNodeInfo& node_info,
    bool current_batch_active) {
    const auto& graph_node = compiled.graph.node(node_id);
    const bool multi_source_is_image_only =
        node_info.source_shape_types.empty() ||
        std::all_of(
            node_info.source_shape_types.begin(),
            node_info.source_shape_types.end(),
            [](const int shape_type) {
                return shape_type == static_cast<int>(ShapeType::Image);
            });
    const bool source_is_image =
        node_info.shape_type == static_cast<int>(ShapeType::Image);
    const bool is_canonical_layer_source =
        (dynamic_cast<const SourceNode*>(&graph_node) != nullptr && source_is_image) ||
        (dynamic_cast<const MultiSourceNode*>(&graph_node) != nullptr && multi_source_is_image_only);
    // Frame-bound image layers carry sampled transforms and external GPU
    // assets.  Keep them on SourceNode's native image path until the fused
    // storage-image batch has an equivalent descriptor/readback contract for
    // those assets.  The native path remains device-local and supports the
    // same Vulkan/NVENC production lane; batching static-compatible layers is
    // still allowed below.
    const bool frame_bound_image_source =
        source_is_image && node_info.binding_meta.has_binding();
    if (frame_bound_image_source) return false;
    const bool is_text_layer_source =
        node_info.kind == RenderGraphNodeKind::TextRun &&
        dynamic_cast<const TextRunNode*>(&graph_node) != nullptr;
    return ((is_canonical_layer_source || is_text_layer_source) &&
            (node_info.kind == RenderGraphNodeKind::Source ||
             node_info.kind == RenderGraphNodeKind::Video ||
             node_info.kind == RenderGraphNodeKind::TextRun)) ||
           (current_batch_active &&
            (dynamic_cast<const TransformNode*>(&graph_node) != nullptr ||
             dynamic_cast<const CompositeNode*>(&graph_node) != nullptr));
}

bool fused_batch_payload_compatible(
    const CompiledFrameGraph& compiled,
    const std::vector<GraphNodeId>& current_batch,
    const CompiledNodeInfo& candidate) {
    const bool batch_has_text = std::any_of(
        current_batch.begin(), current_batch.end(),
        [&](GraphNodeId id) {
            return id < compiled.nodes.size() &&
                   compiled.nodes[id].kind == RenderGraphNodeKind::TextRun;
        });
    // Text batches only fuse text runs; never swallow CompositeNodes into a text batch
    if (batch_has_text && candidate.kind == RenderGraphNodeKind::Composite) {
        return false;
    }
    const bool candidate_text = candidate.kind == RenderGraphNodeKind::TextRun;
    // Distinct TextRun nodes represent independent text layers (e.g. watermark, subtitles)
    // which produce distinct framebuffers for distinct Composite nodes.
    // Fusing them into a single batch causes all but the last node to be skipped,
    // starving upstream composite nodes of their input framebuffers.
    if (batch_has_text && candidate_text) {
        return false;
    }
    const bool candidate_image =
        candidate.kind == RenderGraphNodeKind::Source ||
        candidate.kind == RenderGraphNodeKind::Video;
    if (!candidate_text && !candidate_image) return true;

    for (const auto existing_id : current_batch) {
        if (existing_id >= compiled.nodes.size()) continue;
        const auto existing_kind = compiled.nodes[existing_id].kind;
        if (candidate_text &&
            (existing_kind == RenderGraphNodeKind::Source ||
             existing_kind == RenderGraphNodeKind::Video)) {
            return false;
        }
        if (candidate_image && existing_kind == RenderGraphNodeKind::TextRun) {
            return false;
        }
    }
    return true;
}

// A fused batch publishes exactly ONE output: its root. Every other member is
// skipped at execution time, so a member whose output is consumed OUTSIDE the
// batch hands that consumer a null framebuffer — and a Composite with a
// missing top silently degrades to a pass-through, which drops a whole layer
// from the finished frame while every node still reports success (observed:
// "background + foreground + watermark" clips finished without the watermark
// and without the foreground, because the text member was fused under a
// Transform root its own consumer did not read).
//
// An island is therefore only sound when each non-root member's consumers all
// live inside it; otherwise the members stay standalone and execute normally.
[[nodiscard]] bool batch_is_sound(
    const CompiledFrameGraph& compiled,
    const std::vector<GraphNodeId>& members) {
    if (members.size() < 2) return false;
    const GraphNodeId root = members.back();
    for (GraphNodeId member : members) {
        if (member == root || member >= compiled.nodes.size()) continue;
        for (GraphNodeId consumer : compiled.nodes[member].consumers) {
            if (std::find(members.begin(), members.end(), consumer) ==
                members.end()) {
                return false;
            }
        }
    }
    return true;
}

std::vector<GraphNodeId> collect_layer_batches(CompiledFrameGraph& compiled) {
    std::vector<GraphNodeId> current_fused_batch;
    for (const auto& level : compiled.levels) {
        for (GraphNodeId node_id : level) {
            if (node_id >= compiled.nodes.size() || !compiled.nodes[node_id].reachable) continue;
            const auto& node_info = compiled.nodes[node_id];
            CompiledOperation operation = make_base_operation(compiled, node_id);
            resolve_operation_recorder(compiled, node_info, operation);
            bool candidate_fusible = is_fusible_layer_node(
                compiled, node_id, node_info, !current_fused_batch.empty());
            if (candidate_fusible && !fused_batch_payload_compatible(
                    compiled, current_fused_batch, node_info)) {
                if (batch_is_sound(compiled, current_fused_batch)) {
                    CompiledLayerBatch batch;
                    batch.member_nodes = std::move(current_fused_batch);
                    batch.root_node = batch.member_nodes.back();
                    if (batch.root_node < compiled.nodes.size()) {
                        if (const auto* resource = compiled.resource_table().resource_for(batch.root_node)) {
                            batch.output_physical_slot = resource->physical_slot;
                        }
                    }
                    batch.is_gpu_fused = true;
                    compiled.program.layer_batches.push_back(std::move(batch));
                }
                current_fused_batch.clear();
                candidate_fusible = is_fusible_layer_node(
                    compiled, node_id, node_info, false);
            }
            if (candidate_fusible) {
                current_fused_batch.push_back(node_id);
            } else if (!current_fused_batch.empty()) {
                if (batch_is_sound(compiled, current_fused_batch)) {
                    CompiledLayerBatch batch;
                    batch.member_nodes = std::move(current_fused_batch);
                    batch.root_node = batch.member_nodes.back();
                    if (batch.root_node < compiled.nodes.size()) {
                        if (const auto* resource = compiled.resource_table().resource_for(batch.root_node)) {
                            batch.output_physical_slot = resource->physical_slot;
                        }
                    }
                    batch.is_gpu_fused = true;
                    compiled.program.layer_batches.push_back(std::move(batch));
                }
                current_fused_batch.clear();
            }
        }
    }
    return current_fused_batch;
}

void flush_tail_fused_batch(
    CompiledFrameGraph& compiled,
    std::vector<GraphNodeId> current_fused_batch) {
    if (current_fused_batch.empty()) return;
    if (!batch_is_sound(compiled, current_fused_batch)) return;
    CompiledLayerBatch batch;
    batch.member_nodes = std::move(current_fused_batch);
    batch.root_node = batch.member_nodes.back();
    if (batch.root_node < compiled.nodes.size()) {
        if (const auto* resource = compiled.resource_table().resource_for(batch.root_node)) {
            batch.output_physical_slot = resource->physical_slot;
        }
    }
    batch.is_gpu_fused = true;
    compiled.program.layer_batches.push_back(std::move(batch));
}

void lower_layer_batches(CompiledFrameGraph& compiled) {
    for (auto& batch : compiled.program.layer_batches) {
        if (!batch.is_gpu_fused || batch.member_nodes.empty()) continue;
        CompiledLayerInstance pending_instance;
        bool has_pending = false;
        for (GraphNodeId member_id : batch.member_nodes) {
            if (member_id >= compiled.nodes.size()) continue;
            compiled.nodes[member_id].lowered_into_batch = true;
            compiled.nodes[member_id].execution_owner = ExecutionOwner::Fused;
            const auto& info = compiled.nodes[member_id];
            const bool is_source =
                info.kind == RenderGraphNodeKind::Source ||
                info.kind == RenderGraphNodeKind::TextRun ||
                info.kind == RenderGraphNodeKind::Video;
            if (is_source) {
                if (has_pending) batch.instances.push_back(pending_instance);
                has_pending = true;
                pending_instance = CompiledLayerInstance{};
                pending_instance.node = member_id;
                // This is the canonical dense resource key for the fused
                // data-plane. It is deliberately independent of graph node
                // ids and is validated against the materialized resource
                // vector by the worker-local scratch path.
                pending_instance.resource_index =
                    static_cast<std::uint32_t>(batch.instances.size());
                pending_instance.opacity = 1.0f;
                if (info.predicted_bbox) pending_instance.dst_bounds = *info.predicted_bbox;
            } else if (has_pending && info.kind == RenderGraphNodeKind::Transform) {
                pending_instance.transform_index = static_cast<std::uint32_t>(member_id);
            } else if (has_pending && info.kind == RenderGraphNodeKind::Composite) {
                // Composite closes the logical layer without adding a new instance payload.
            }
        }
        if (has_pending) batch.instances.push_back(pending_instance);
    }
}

void build_standalone_operations(CompiledFrameGraph& compiled) {
    for (const auto& level : compiled.levels) {
        for (GraphNodeId node_id : level) {
            if (node_id >= compiled.nodes.size() || !compiled.nodes[node_id].reachable) continue;
            if (compiled.nodes[node_id].lowered_into_batch) continue;
            CompiledOperation operation = make_base_operation(compiled, node_id);
            compiled.program.operations.push_back(std::move(operation));
            compiled.nodes[node_id].execution_owner = ExecutionOwner::Standalone;
            compiled.nodes[node_id].elimination_reason = EliminationReason::None;
        }
    }
}

void emit_fused_batch_operations(CompiledFrameGraph& compiled) {
    for (const auto& batch : compiled.program.layer_batches) {
        if (batch.is_gpu_fused && !batch.instances.empty() && batch.root_node != k_invalid_node) {
            CompiledOperation batch_op;
            batch_op.node = batch.root_node;
            if (batch.root_node < compiled.nodes.size()) {
                batch_op.stable_node = compiled.nodes[batch.root_node].stable_node_id;
            }
            batch_op.output_physical_slot = batch.output_physical_slot;
            batch_op.is_fused = true;
            compiled.program.operations.push_back(std::move(batch_op));
        }
    }
}

void validate_operation_coverage(CompiledFrameGraph& compiled) {
    std::vector<const CompiledOperation*> operation_for_node(compiled.nodes.size(), nullptr);
    for (const auto& op : compiled.program.operations) {
        if (op.node >= operation_for_node.size()) {
            throw std::runtime_error(
                "GraphCompilePipeline: execution program contains an invalid node " +
                std::to_string(op.node));
        }
        if (operation_for_node[op.node] != nullptr) {
            throw std::runtime_error(
                "GraphCompilePipeline: execution program contains duplicate operations for node " +
                std::to_string(op.node));
        }
        operation_for_node[op.node] = &op;
    }

    std::vector<bool> fused_owner(compiled.nodes.size(), false);
    std::vector<bool> fused_root(compiled.nodes.size(), false);
    for (const auto& batch : compiled.program.layer_batches) {
        if (!batch.is_gpu_fused) continue;
        if (batch.root_node >= operation_for_node.size() ||
            operation_for_node[batch.root_node] == nullptr ||
            !operation_for_node[batch.root_node]->is_fused) {
            throw std::runtime_error("GraphCompilePipeline: fused batch has no fused root operation");
        }
        fused_root[batch.root_node] = true;
        for (const auto member : batch.member_nodes) {
            if (member == batch.root_node) continue;
            if (member >= fused_owner.size() || fused_owner[member]) {
            throw std::runtime_error("GraphCompilePipeline: node belongs to multiple fused batches");
            }
            fused_owner[member] = true;
        }
    }

    for (GraphNodeId node_id = 0; node_id < compiled.nodes.size(); ++node_id) {
        const auto& info = compiled.nodes[node_id];
        if (!info.reachable) continue;
        const bool standalone = operation_for_node[node_id] != nullptr;
        const bool fused = fused_owner[node_id];
        const bool is_fused_root = fused_root[node_id];
        if (is_fused_root) {
            if (!standalone || fused) {
                throw std::runtime_error(
                    "GraphCompilePipeline: fused batch root has invalid execution representations: node=" +
                    std::to_string(node_id) + " name='" + info.name + "' kind=" +
                    std::string(to_string(info.kind)));
            }
            if (!info.lowered_into_batch) {
                throw std::runtime_error(
                "GraphCompilePipeline: fused batch root was not lowered for node " +
                    std::to_string(node_id));
            }
            continue;
        }
        if (standalone == fused) {
            throw std::runtime_error(
                "GraphCompilePipeline: reachable node disappeared or has multiple execution representations: node=" +
                std::to_string(node_id) + " name='" + info.name + "' kind=" +
                std::string(to_string(info.kind)));
        }
        if (info.lowered_into_batch != fused) {
            throw std::runtime_error(
                "GraphCompilePipeline: lowered_into_batch metadata disagrees with execution program for node " +
                std::to_string(node_id));
        }
    }
    GraphCompilePipeline::validate_compiled_program_coverage(compiled);
}

void compute_has_fused_passes(CompiledFrameGraph& compiled) {
    compiled.program.has_fused_passes = false;
    for (const auto& batch : compiled.program.layer_batches) {
        if (batch.is_gpu_fused && !batch.instances.empty() && batch.root_node != k_invalid_node) {
            compiled.program.has_fused_passes = true;
            break;
        }
    }
}

void compute_static_bake_islands(CompiledFrameGraph& compiled) {
    std::vector<bool> is_node_static(compiled.nodes.size(), false);
    for (const auto& level : compiled.levels) {
        for (GraphNodeId node_id : level) {
            if (node_id >= compiled.nodes.size() || !compiled.nodes[node_id].reachable) continue;
            const auto& node_info = compiled.nodes[node_id];
            bool all_inputs_static = true;
            for (GraphNodeId in_id : node_info.inputs) {
                if (in_id < compiled.nodes.size() && !is_node_static[in_id]) {
                    all_inputs_static = false;
                    break;
                }
            }
            if (node_info.cache_policy.reusable_across_frames() && all_inputs_static) {
                is_node_static[node_id] = true;
            }
        }
    }
    for (GraphNodeId node_id = 0; node_id < compiled.nodes.size(); ++node_id) {
        if (!is_node_static[node_id] || !compiled.nodes[node_id].reachable) continue;
        const auto& node_info = compiled.nodes[node_id];
        bool is_maximal_root = node_info.consumers.empty();
        for (GraphNodeId consumer_id : node_info.consumers) {
            if (consumer_id < compiled.nodes.size() && !is_node_static[consumer_id]) {
                is_maximal_root = true;
                break;
            }
        }
        if (is_maximal_root) {
            StaticSubgraphBakePass bake;
            bake.root_node = node_id;
            bake.static_fingerprint = node_info.static_key.digest();
            bake.is_baked = false;
            compiled.program.static_bakes.push_back(bake);
        }
    }
}

void compute_fully_recorded(CompiledFrameGraph& compiled) {
    compiled.program.fully_recorded =
        !compiled.program.operations.empty() && !compiled.program.has_fused_passes;
    for (const auto& op : compiled.program.operations) {
        if (!op.has_compiled_execute() && !op.is_fused) {
            compiled.program.fully_recorded = false;
            break;
        }
    }
}

void build_compiled_frame_program(CompiledFrameGraph& compiled) {
    compiled.program = CompiledFrameProgram{};
    compiled.program.levels = compiled.levels;
    std::size_t count = 0;
    for (const auto& level : compiled.levels) count += level.size();
    compiled.program.operations.reserve(count);

    std::vector<GraphNodeId> tail_fused_batch = collect_layer_batches(compiled);
    flush_tail_fused_batch(compiled, tail_fused_batch);
    lower_layer_batches(compiled);
    // Lowering is canonical only when every fused instance has a valid source
    // node and a dense resource key. Fail at compile time; execution must not
    // infer or silently repair bindings.
    for (const auto& batch : compiled.program.layer_batches) {
        if (!batch.is_gpu_fused) continue;
        for (const auto& instance : batch.instances) {
            if (instance.node >= compiled.nodes.size() ||
                instance.resource_index >= batch.instances.size()) {
                throw std::runtime_error(
                    "GraphCompilePipeline: fused layer instance has invalid "
                    "node/resource binding");
            }
        }
    }
    build_standalone_operations(compiled);
    emit_fused_batch_operations(compiled);
    validate_operation_coverage(compiled);
    // Stage 3: build the dense node->operation index once at compile time.
    compiled.program.rebuild_operation_index(compiled.nodes.size());
    compiled.program.rebuild_layer_batch_index(compiled.nodes.size());
    compute_has_fused_passes(compiled);
    compute_static_bake_islands(compiled);
    compute_fully_recorded(compiled);
}
} // namespace
