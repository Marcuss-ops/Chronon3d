void execute_single_node(
    ExecutionState& state,
    RenderGraph& graph,
    RenderGraphContext& ctx,
    const std::pmr::vector<PreResolvedNode>& level_resolved,
    GraphNodeId id,
    size_t level_index,
    RenderCounters* parent_counters,
    FramebufferPool* parent_pool,
    std::pmr::vector<std::atomic_size_t>& consumer_remaining,
    double* out_cache_ms,
    double* out_dirty_ms,
    double* out_telemetry_ms,
    double* out_execute_ms,
    double* out_predicted_bbox_ms,
    double* out_clone_context_ms,
    double* out_state_assign_ms,
    const CompiledFrameGraph& compiled
) {
    // Stage 3: fine-grained per-node timing is no longer part of the normal
    // execution contract. Keep the ABI-compatible pointer surface for
    // diagnostic callers, but the production dispatcher passes nullptr and
    // pays no region Clock::now() calls here.
    (void)out_cache_ms;
    (void)out_dirty_ms;
    (void)out_telemetry_ms;
    (void)out_execute_ms;
    (void)out_predicted_bbox_ms;
    (void)out_clone_context_ms;
    (void)out_state_assign_ms;

    auto& node = graph.node(id);
    const auto& input_ids = graph.inputs(id);
    const auto& pr = level_resolved[level_index];

    if (id < ctx.node_exec.early_exit_skip.size() && ctx.node_exec.early_exit_skip[id]) {
        commit_transparent_skip(
            state, id, ctx, parent_pool,
            SkipReason::EarlyExit,
            graph.node(id).name());
        return;
    }

    if (id < compiled.program.interior_node_skip.size() &&
        compiled.program.interior_node_skip[id]) {
        if (ctx.node_exec.counters) {
            ctx.node_exec.counters->nodes_executed.fetch_add(1, std::memory_order_relaxed);
        }
        commit_transparent_skip(
            state, id, ctx, parent_pool,
            SkipReason::StaticBaked,
            graph.node(id).name());
        return;
    }

    if (id < compiled.nodes.size() && compiled.nodes[id].lowered_into_batch &&
        ctx.services.backend && ctx.services.backend->is_batching_supported()) {
        const CompiledLayerBatch* batch_for_root = nullptr;
        for (const auto& b : compiled.program.layer_batches) {
            if (b.is_gpu_fused && b.root_node == id) {
                batch_for_root = &b;
                break;
            }
        }
        if (!batch_for_root) {
            commit_transparent_skip(
                state, id, ctx, parent_pool,
                SkipReason::StaticBaked,
                graph.node(id).name());
            return;
        }

        // Mutable node execution state is worker-local. Never write identity,
        // scratch or reporter fields into the shared parent context.
        RenderGraphContext batch_ctx = ctx.clone_for_node_execution();
        batch_ctx.node_exec.text_bbox_reporter = &state.text_bbox_reporter;
        if (id < compiled.nodes.size() && compiled.nodes[id].reachable) {
            batch_ctx.node_exec.current_identity = NodeIdentity{
                compiled.graph_instance_id, compiled.nodes[id].stable_node_id};
        }
        detail::execute_fused_batch(
            state, graph, batch_ctx, *batch_for_root, id,
            parent_pool, parent_counters, compiled);
        return;
    }

    profiling::ProfilingGuard node_guard(parent_counters, parent_pool);

    auto cache_eval = evaluate_cache(
        node, ctx,
        pr.input_hash,
        pr.inputs_frame_dependent,
        pr.has_cacheable_inputs,
        id
    );

    if (diag_exec_logging_enabled()) {
        spdlog::info("[DIAG-exec] frame={} node='{}' id={} kind='{}' cache='{}' frame_dep={} use_cache={} result_ptr={}",
            static_cast<int>(ctx.frame_input.frame), node.name(), id, to_string(node.kind()),
            cache_eval.cache_status, cache_eval.node_frame_dependent ? 1 : 0,
            cache_eval.use_cache ? 1 : 0,
            cache_eval.result ? fmt::ptr(cache_eval.result.get()) : "null");
    }

    auto predicted_bbox = node.predicted_bbox(ctx, pr.input_bboxes);

    const bool cache_hit_fast_path =
        cache_eval.result &&
        cache_eval.cache_status == "hit" &&
        !cache_eval.node_frame_dependent &&
        !ctx.policy.tile_execution_enabled;

    if (cache_hit_fast_path) {
        commit_node_state(state, id, cache_eval, predicted_bbox);
        emit_node_records(
            ctx, node,
            cache_eval.key,
            cache_eval.result,
            predicted_bbox,
            cache_eval.cache_status,
            cache_eval.is_cacheable,
            static_cast<int>(input_ids.size()),
            0.0
        );
        if (ctx.node_exec.counters) {
            ctx.node_exec.counters->nodes_executed.fetch_add(1, std::memory_order_relaxed);
        }
        return;
    }

    if (ctx.policy.tile_execution_enabled && ctx.node_exec.active_tile_clip &&
        predicted_bbox && !predicted_bbox->is_empty()) {
        const auto& tile = *ctx.node_exec.active_tile_clip;
        const auto& bbox = *predicted_bbox;
        const bool bbox_intersects_tile =
            bbox.x0 < tile.x1 && bbox.x1 > tile.x0 &&
            bbox.y0 < tile.y1 && bbox.y1 > tile.y0;
        if (!bbox_intersects_tile) {
            commit_transparent_skip(
                state, id, ctx, parent_pool, SkipReason::TilePruned,
                /*node_name=*/{}, /*bbox_override=*/predicted_bbox);
            return;
        }
    }

    // Single worker-local mutable context for the actual node execution.
    RenderGraphContext node_ctx = ctx.clone_for_node_execution();
    node_ctx.node_exec.text_bbox_reporter = &state.text_bbox_reporter;
    node_ctx.node_exec.planned_physical_slot = nullptr;
    const auto& resource_table = compiled.resource_table();
    if (id < resource_table.resources.size()) {
        const auto& allocation = resource_table.resources[id];
        if (allocation.aliasable() &&
            allocation.physical_slot != kInvalidPhysicalAllocationId &&
            allocation.physical_slot < state.physical_slots.size()) {
            node_ctx.node_exec.planned_physical_slot =
                &state.physical_slots[allocation.physical_slot];
        }
    }

    if (id < compiled.nodes.size() && compiled.nodes[id].reachable) {
        node_ctx.node_exec.current_identity = NodeIdentity{
            compiled.graph_instance_id,
            compiled.nodes[id].stable_node_id
        };
        const auto& node_info = compiled.nodes[id];
        node_ctx.node_exec.current_shape_processor = {};
        node_ctx.node_exec.current_shape_processors = {};
        node_ctx.node_exec.current_effect_processors = {};
        node_ctx.node_exec.processor_snapshot = compiled.processor_snapshot;
        if (node_info.shape_processors_count > 0 &&
            node_info.shape_processors_offset <= compiled.shape_processor_table.size() &&
            node_info.shape_processors_count <=
                compiled.shape_processor_table.size() - node_info.shape_processors_offset) {
            const auto* shape_begin = compiled.shape_processor_table.data() +
                node_info.shape_processors_offset;
            node_ctx.node_exec.current_shape_processors = {
                shape_begin, node_info.shape_processors_count};
            node_ctx.node_exec.current_shape_processor = shape_begin[0];
        }
        if (node_info.effect_processors_count > 0 &&
            node_info.effect_processors_offset <= compiled.effect_processor_table.size() &&
            node_info.effect_processors_count <=
                compiled.effect_processor_table.size() - node_info.effect_processors_offset) {
            const auto* effect_begin = compiled.effect_processor_table.data() +
                node_info.effect_processors_offset;
            node_ctx.node_exec.current_effect_processors = {
                effect_begin, node_info.effect_processors_count};
        }
        node_ctx.node_exec.processor_bindings_compiled = true;
    } else {
        node_ctx.node_exec.current_shape_processor = {};
        node_ctx.node_exec.current_shape_processors = {};
        node_ctx.node_exec.current_effect_processors = {};
        node_ctx.node_exec.processor_snapshot = compiled.processor_snapshot;
        node_ctx.node_exec.processor_bindings_compiled = false;
    }

    if (ctx.policy.dirty_rects_enabled) {
        node_ctx.node_exec.clip_rect = compute_dirty_clip(ctx, node, predicted_bbox);
    } else {
        node_ctx.node_exec.clip_rect = predicted_bbox;
    }

    node_ctx.node_exec.reusable_inputs.clear();
    node_ctx.node_exec.reusable_bottom.reset();
    for (size_t j = 0; j < input_ids.size(); ++j) {
        const GraphNodeId input_id = input_ids[j];
        if (contains_index(state.temp, input_id) && state.temp[input_id]) {
            const auto* resource = resource_table.resource_for(input_id);
            const auto transfer_consumer = resource
                ? resource->ownership_transfer_consumer()
                : std::nullopt;
            const bool compiled_sole_consumer =
                transfer_consumer && *transfer_consumer == id;
            if ((compiled_sole_consumer ||
                 consumer_remaining[input_id].load(std::memory_order_relaxed) == 1) &&
                state.temp[input_id].use_count() == 1) {
                node_ctx.node_exec.reusable_inputs.push_back(state.temp[input_id].get());
                if (j == 0) node_ctx.node_exec.reusable_bottom = state.temp[input_id];
            }
        }
    }

    std::optional<TemporalSampleKey> sample_key;
    if (ctx.frame_input.temporal_key != TemporalSampleKey{}) {
        sample_key = ctx.frame_input.temporal_key;
    }
    std::optional<ScopedNodeMemory> node_memory_scope;
    if (state.node_memory_tracker) {
        node_memory_scope.emplace(
            *state.node_memory_tracker,
            memory_node_id(compiled, id, node.name()),
            sample_key,
            0);
    }

    // Keep exactly one node-execute duration for telemetry. The six auxiliary
    // timing regions were removed from the production hot path above.
    const double duration_ms = run_node(
        node, node_ctx,
        pr.inputs, pr.input_bboxes,
        cache_eval.use_cache,
        cache_eval.key,
        cache_eval.result,
        ctx,
        parent_pool,
        &compiled.nodes[id].stable_node_id
    );
    if (state.node_memory_tracker && cache_eval.result) {
        node_memory_scope->set_live_bytes(cache_eval.result->size_bytes());
    }

    if (node.kind() == RenderGraphNodeKind::TextRun &&
        cache_eval.result && predicted_bbox && !predicted_bbox->is_empty()) {
        const Framebuffer* fb_ptr = cache_eval.result.get();
        if (fb_ptr && fb_ptr->width() > 0 && fb_ptr->height() > 0) {
            if (auto expanded_bbox = reconcile_text_bbox_after_render(
                    node, *fb_ptr, predicted_bbox,
                    ctx.node_exec.counters,
                    state.text_bbox_reporter,
                    node_ctx.node_exec.actual_ink_bbox)) {
                predicted_bbox = *expanded_bbox;
            }
        }
    }

    emit_node_records(
        ctx, node,
        cache_eval.key,
        cache_eval.result,
        predicted_bbox,
        cache_eval.cache_status,
        cache_eval.is_cacheable,
        static_cast<int>(input_ids.size()),
        duration_ms
    );

    commit_node_state(state, id, cache_eval, predicted_bbox);
}
