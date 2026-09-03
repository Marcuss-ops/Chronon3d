[[nodiscard]] static std::shared_ptr<Framebuffer> execute_internal(
    CompiledFrameGraph& compiled,
    RenderGraphContext& ctx,
    RenderSession& session,
    FrameArena& arena,
    ExecutionScheduler& scheduler
) {
    auto& graph = compiled.graph;
    const auto& levels = compiled.program.empty()
        ? compiled.levels
        : compiled.program.levels;
    const auto& consumer_counts = compiled.consumer_counts;
    const auto output = compiled.output;

    if (compiled.empty()) {
        session.publish_last_frame_error(NodeExecutionError{
            RenderBackendErrorCode::InvalidInput,
            "frame_graph",
            "compiled frame graph is empty"
        });
        return nullptr;
    }

    auto workspace_lease = session.execution_workspaces->acquire(
        static_cast<std::uint64_t>(std::max<std::int64_t>(0, ctx.frame_input.frame.integral())));
    auto& workspace = workspace_lease.workspace();
    auto* res = arena.resource();
    struct ArenaGuard {
        FrameArena& arena;
        ~ArenaGuard() { arena.reset(); }
    } guard{arena};

    const size_t node_count = graph.size();
    ExecutionState state(workspace, session.memory_tracker.get());
    state.temp.resize(node_count);
    state.resolved_key_digest.assign(node_count, 0);
    state.resolved_frame_dependent.assign(node_count, 0);
    state.resolved_cache_hit.assign(node_count, 0);
    state.resolved_bboxes.resize(node_count);

    const auto& resource_table = compiled.resource_table();
    state.physical_slots.resize(resource_table.physical_slot_count);
    for (auto& slot : state.physical_slots) {
        slot.reset();
    }

    const auto t_fb0 = profiling::now();
    init_shared_transparent_fb(state, ctx, res);

    auto consumer_remaining = init_consumer_remaining(node_count, consumer_counts, res);
    const auto t_fb1 = profiling::now();
    if (ctx.node_exec.counters) {
        ctx.node_exec.counters->framebuffer_lifetime_wall_ms.fetch_add(
            static_cast<uint64_t>(std::llround(profiling::duration_ms(t_fb0, t_fb1))),
            std::memory_order_relaxed);
    }

    auto* parent_counters = ctx.node_exec.counters;
    auto* parent_pool = ctx.services.framebuffer_pool.get();

    if (parent_counters) {
        const auto logical_count = static_cast<std::uint64_t>(
            resource_table.logical_resource_count);
        const auto physical_count = static_cast<std::uint64_t>(
            resource_table.physical_slot_count);
        const auto logical_bytes = static_cast<std::uint64_t>(
            resource_table.logical_bytes);
        const auto physical_bytes = static_cast<std::uint64_t>(
            resource_table.planned_physical_bytes);

        parent_counters->logical_resource_count.store(
            logical_count, std::memory_order_relaxed);
        parent_counters->physical_resource_slot_count.store(
            physical_count, std::memory_order_relaxed);
        parent_counters->logical_resource_bytes.store(
            logical_bytes, std::memory_order_relaxed);
        parent_counters->physical_resource_bytes.store(
            physical_bytes, std::memory_order_relaxed);
        parent_counters->alias_saved_bytes.store(
            logical_bytes > physical_bytes ? logical_bytes - physical_bytes : 0,
            std::memory_order_relaxed);
        parent_counters->alias_reuse_count.store(
            logical_count > physical_count ? logical_count - physical_count : 0,
            std::memory_order_relaxed);
        parent_counters->new_resource_slot_count.store(
            physical_count, std::memory_order_relaxed);
        parent_counters->arena_peak_bytes.store(
            static_cast<std::uint64_t>(resource_table.peak_live_bytes),
            std::memory_order_relaxed);
    }

    ctx.frame_error = std::make_shared<std::optional<NodeExecutionError>>();

    if (compiled.program.fully_recorded) {
        execute_compiled_program(compiled.program, ctx, state, parent_counters);
    } else {
        execute_levels(graph, ctx, state, scheduler, levels, consumer_remaining,
                       parent_counters, parent_pool, res, compiled);
    }

    if (parent_pool && state.node_memory_tracker) {
        const auto pool = parent_pool->stats();
        state.node_memory_tracker->record_pool(NodeMemoryPoolSnapshot{
            .current_bytes = pool.current_bytes,
            .retained_bytes = pool.retained_bytes,
            .peak_retained_bytes = pool.peak_retained_bytes,
            .total_allocations = pool.total_allocations,
            .total_reuses = pool.total_reuses,
            .total_returns = pool.total_returns,
            .evicted_bytes = pool.evicted_bytes,
        });
    }

    if (ctx.frame_error && ctx.frame_error->has_value()) {
        const auto& err = ctx.frame_error->value();
        session.publish_last_frame_error(err);
        spdlog::error(
            "[executor] frame {} failed: node '{}' error [{}] {}",
            static_cast<int>(ctx.frame_input.frame),
            err.node_name,
            render_backend_error_code_name(err.backend_code),
            err.message);
        return nullptr;
    }

    return state.temp[output];
}
