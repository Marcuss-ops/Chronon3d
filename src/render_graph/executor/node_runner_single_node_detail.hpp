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
    const CompiledFrameGraph& compiled,
    std::uint64_t* local_nodes_executed
) {
    // OPT-DEAD-TIMINGS — the 7 dead per-node timing out-params are removed.
    // Fine-grained per-node timing left the execution contract in Stage 3;
    // the production dispatcher never passed anything but nullptr for them.

    auto& node = graph.node(id);
    const auto& input_ids = graph.inputs(id);
    const auto& pr = level_resolved[level_index];
    const auto record_node_executed = [&]() {
        if (local_nodes_executed) {
            ++(*local_nodes_executed);
        } else if (parent_counters) {
            parent_counters->nodes_executed.fetch_add(
                1, std::memory_order_relaxed);
        }
    };
    const auto retain_static_bake = [&]() {
        if (id >= state.static_bake_root_index.size()) return;
        const auto bake_index = state.static_bake_root_index[id];
        if (bake_index >= compiled.program.static_bakes.size()) return;
        const auto& bake = compiled.program.static_bakes[bake_index];
        if (!bake.runtime || !state.temp[id]) return;
        std::lock_guard lock(bake.runtime->mutex);
        if (!bake.runtime->baked_surface) {
            bake.runtime->baked_surface = state.temp[id];
            bake.runtime->resolved_key_digest = state.resolved_key_digest[id];
            bake.runtime->resolved_bbox = state.resolved_bboxes[id];
        }
    };

    // Typed programs own frame activity.  A graph binding is the stable
    // bridge from a render-graph node to the compiler layer id; do not consult
    // a materialized Scene for this decision when the typed block is present.
    const compiler::SampledLayerParameters* typed_layer = nullptr;
    if (ctx.frame_parameters && id < compiled.nodes.size() &&
        compiled.nodes[id].binding_meta.has_binding()) {
        typed_layer = compiler::find_layer(
            *ctx.frame_parameters, compiled.nodes[id].binding_meta.layer_index);
        if (!typed_layer || !typed_layer->active) {
            commit_transparent_skip(
                state, id, ctx, parent_pool, SkipReason::EarlyExit,
                graph.node(id).name());
            return;
        }
    }

    if (id < ctx.node_exec.early_exit_skip.size() && ctx.node_exec.early_exit_skip[id]) {
        commit_transparent_skip(
            state, id, ctx, parent_pool,
            SkipReason::EarlyExit,
            graph.node(id).name());
        return;
    }

    if (id < state.static_bake_member_skip.size() &&
        state.static_bake_member_skip[id]) {
        const auto bake_index = id < state.static_bake_root_index.size()
            ? state.static_bake_root_index[id] : UINT32_MAX;
        if (bake_index < compiled.program.static_bakes.size()) {
            const auto& bake = compiled.program.static_bakes[bake_index];
            std::lock_guard lock(bake.runtime->mutex);
            state.temp[id] = bake.runtime->baked_surface;
            state.resolved_key_digest[id] = bake.runtime->resolved_key_digest;
            state.resolved_frame_dependent[id] = 0;
            state.resolved_cache_hit[id] = 1;
            state.resolved_bboxes[id] = bake.runtime->resolved_bbox;
            record_node_executed();
            return;
        }
        // A member that is not a bake root has no baked surface of its own.
        // Publishing a null skip here starves consumers outside the bake (a
        // Composite then reads no input), so it executes normally instead.
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
        ctx.execution_backend() && ctx.execution_backend()->is_batching_supported()) {
        const CompiledLayerBatch* batch_for_root =
            compiled.program.layer_batch_for(id);
        if (!batch_for_root || batch_for_root->root_node != id) {
            // A lowered batch member that is not its root produces NO output of
            // its own: the batch root publishes the fused destination. If any
            // consumer outside the batch still reads this node, that consumer
            // gets a null input and silently degenerates (a Composite reads no
            // top and passes its bottom through), so name the two ids here.
            if (ctx.frame_input.frame <= 2) {
                spdlog::info(
                    "[batch-member-skip] frame={} node_id={} name='{}' "
                    "batch_root={} batch_members={} consumers={}",
                    static_cast<int>(ctx.frame_input.frame), id,
                    graph.node(id).name(),
                    batch_for_root ? batch_for_root->root_node : k_invalid_node,
                    batch_for_root ? batch_for_root->member_nodes.size() : 0,
                    id < compiled.nodes.size()
                        ? compiled.nodes[id].consumers.size()
                        : 0);
            }
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
        batch_ctx.node_exec.sampled_layer = typed_layer;
        const auto& resource_table = compiled.resource_table();
        if (id == compiled.output &&
            ctx.policy.native_video_encode_surface != runtime::kInvalidRenderSurfaceHandle) {
            if (ctx.node_exec.terminal_video_encode_physical_slot) {
                batch_ctx.node_exec.planned_physical_slot =
                    ctx.node_exec.terminal_video_encode_physical_slot;
            } else {
                batch_ctx.node_exec.terminal_video_encode_surface =
                    ctx.policy.native_video_encode_surface;
            }
            batch_ctx.node_exec.reusable_bottom.reset();
        } else if (id < resource_table.resources.size()) {
            const auto& allocation = resource_table.resources[id];
            if (allocation.aliasable() &&
                allocation.physical_slot != kInvalidPhysicalAllocationId &&
                allocation.physical_slot < state.physical_slots.size()) {
                batch_ctx.node_exec.planned_physical_slot =
                    &state.physical_slots[allocation.physical_slot];
            }
        }
        if (id < compiled.nodes.size() && compiled.nodes[id].reachable) {
            batch_ctx.node_exec.current_identity = NodeIdentity{
                compiled.graph_instance_id, compiled.nodes[id].stable_node_id};
        }
        detail::execute_fused_batch(
            state, graph, batch_ctx, *batch_for_root, id,
            parent_pool, parent_counters, compiled, local_nodes_executed);
        return;
    }

    profiling::ProfilingGuard node_guard(parent_counters, parent_pool);

    // HOT-PATH TAX A — the compiled consumer table says whether this node's
    // per-frame cache key can ever be read (own lookup or a consumer's).
    // Default true when the compiled node info is unavailable.
    const bool cache_key_required =
        id < compiled.nodes.size() && compiled.nodes[id].cache_key_required;

    auto cache_eval = evaluate_cache(
        node, ctx,
        pr.input_hash,
        pr.inputs_frame_dependent,
        pr.has_cacheable_inputs,
        id,
        cache_key_required
    );

    if (diag_exec_logging_enabled()) {
        spdlog::info("[DIAG-exec] frame={} node='{}' id={} kind='{}' cache='{}' frame_dep={} use_cache={} result_ptr={}",
            static_cast<int>(ctx.frame_input.frame), node.name(), id, to_string(node.kind()),
            cache_status_name(cache_eval.cache_status),
            cache_eval.node_frame_dependent ? 1 : 0,
            cache_eval.use_cache ? 1 : 0,
            cache_eval.result ? fmt::ptr(cache_eval.result.get()) : "null");
    }

    auto predicted_bbox = node.predicted_bbox(ctx, pr.input_bboxes);

    const bool cache_hit_fast_path =
        cache_eval.result &&
        cache_eval.cache_status == CacheEvaluationStatus::Hit &&
        !cache_eval.node_frame_dependent &&
        !ctx.policy.tile_execution_enabled;

    if (cache_hit_fast_path) {
        commit_node_state(state, id, cache_eval, predicted_bbox);
        retain_static_bake();
        record_node_executed();
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
    node_ctx.node_exec.sampled_layer = typed_layer;
    node_ctx.node_exec.planned_physical_slot = nullptr;
    const auto& resource_table = compiled.resource_table();
    const bool terminal_encode_binding =
        id == compiled.output &&
        ctx.policy.native_video_encode_surface != runtime::kInvalidRenderSurfaceHandle;
    if (terminal_encode_binding && ctx.node_exec.terminal_video_encode_physical_slot) {
        node_ctx.node_exec.planned_physical_slot =
            ctx.node_exec.terminal_video_encode_physical_slot;
        node_ctx.node_exec.terminal_video_encode_surface =
            runtime::kInvalidRenderSurfaceHandle;
        node_ctx.node_exec.reusable_bottom.reset();
    } else if (terminal_encode_binding) {
        node_ctx.node_exec.terminal_video_encode_surface =
            ctx.policy.native_video_encode_surface;
        node_ctx.node_exec.reusable_bottom.reset();
    } else if (id < resource_table.resources.size()) {
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
        // OPT-CLONE-BORROW — the clone's processor_snapshot is (re)assigned
        // from the compiled graph, which owns the snapshot for the entire
        // execution; per-node refcount traffic buys nothing here. The raw
        // alias is written via shared_ptr aliasing-ctor-free borrow: the
        // compiled graph keeps the only strong reference and outlives every
        // node_ctx (synchronous execution inside execute_internal).
        node_ctx.node_exec.processor_snapshot =
            std::shared_ptr<const ::chronon3d::renderer::ProcessorRegistrySnapshot>(
                std::shared_ptr<void>(), compiled.processor_snapshot.get());
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
        // OPT-CLONE-BORROW — non-owning alias, see the reachable branch above.
        node_ctx.node_exec.processor_snapshot =
            std::shared_ptr<const ::chronon3d::renderer::ProcessorRegistrySnapshot>(
                std::shared_ptr<void>(), compiled.processor_snapshot.get());
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
    // HOT-PATH TAX (TICKET-HOTPATH-ZERO-ALLOC-P1) — canonical allocation-free
    // id: memory_node_id() formats "g<n>:n<m>" directly into NodeMemoryId;
    // ScopedNodeMemory owns that fixed-size value through the tracker callbacks.
    std::optional<ScopedNodeMemory> node_memory_scope;
    if (state.node_memory_tracker) {
        node_memory_scope.emplace(
            *state.node_memory_tracker,
            memory_node_id(compiled, id, node.name()),
            sample_key,
            0,
            to_string(node.kind()));
        if (cache_eval.cache_status != CacheEvaluationStatus::Hit) {
            for (const FramebufferRef& input : pr.inputs) {
                if (!input) continue;
                node_memory_scope->record_read(
                    static_cast<std::uint64_t>(input->pixel_count()),
                    static_cast<std::uint64_t>(input->size_bytes()));
            }
        }
    }

    run_node(
        node, node_ctx,
        pr.inputs, pr.input_bboxes,
        cache_eval.use_cache,
        cache_eval.key,
        cache_eval.result,
        ctx,
        &compiled.nodes[id].stable_node_id,
        local_nodes_executed
    );
    if (state.node_memory_tracker && cache_eval.result) {
        node_memory_scope->set_live_bytes(cache_eval.result->size_bytes());
        if (cache_eval.cache_status != CacheEvaluationStatus::Hit) {
            node_memory_scope->record_write(
                static_cast<std::uint64_t>(cache_eval.result->pixel_count()),
                static_cast<std::uint64_t>(cache_eval.result->size_bytes()));
        }
    }

    if (node_memory_scope &&
        telemetry::telemetry_capture_level() >= telemetry::TelemetryLevel::Detailed) {
        const auto memory = node_memory_scope->metrics_snapshot();
        telemetry::NodeTelemetryRecord record{};
        record.frame_number = static_cast<int>(ctx.frame_input.frame);
        record.node_name = node.name();
        record.node_type = to_string(node.kind());
        record.cache_status = std::string(cache_status_name(cache_eval.cache_status));
        if (cache_eval.result) {
            record.output_width = cache_eval.result->width();
            record.output_height = cache_eval.result->height();
            record.output_bytes = cache_eval.result->size_bytes();
        }
        record.pixels_read = memory.pixels_read.load(std::memory_order_relaxed);
        record.pixels_written = memory.pixels_written.load(std::memory_order_relaxed);
        record.bytes_read = memory.bytes_read.load(std::memory_order_relaxed);
        record.bytes_written = memory.bytes_written.load(std::memory_order_relaxed);
        record.framebuffer_copies = memory.framebuffer_copies.load(std::memory_order_relaxed);
        record.framebuffer_clears = memory.framebuffer_clears.load(std::memory_order_relaxed);
        record.allocations = memory.allocations.load(std::memory_order_relaxed);
        record.temporary_buffers = memory.temporary_buffers.load(std::memory_order_relaxed);
        telemetry::record_node_telemetry(record);
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

    commit_node_state(state, id, cache_eval, predicted_bbox);
    retain_static_bake();
}
