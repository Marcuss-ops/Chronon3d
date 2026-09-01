namespace {

void dispatch_level_nodes(
    RenderGraph& graph,
    RenderGraphContext& ctx,
    ExecutionState& state,
    ExecutionScheduler& scheduler,
    const std::vector<GraphNodeId>& level,
    const std::pmr::vector<PreResolvedNode>& level_resolved,
    std::pmr::vector<std::atomic_size_t>& consumer_remaining,
    RenderCounters* parent_counters,
    cache::FramebufferPool* parent_pool,
    LevelTimings& timings,
    const CompiledFrameGraph& compiled)
{
    const bool use_parallel = should_execute_level_in_parallel(
        level.size(), scheduler.concurrency());

    if (parent_counters) {
        if (use_parallel) {
            parent_counters->parallel_regions_count.fetch_add(1, std::memory_order_relaxed);
            parent_counters->level_parallel_count.fetch_add(1, std::memory_order_relaxed);
        } else {
            parent_counters->parallel_regions_skipped_small_level.fetch_add(1, std::memory_order_relaxed);
            parent_counters->level_sequential_count.fetch_add(1, std::memory_order_relaxed);
        }
    }

    if (use_parallel) {
        std::atomic<int> active_parallel_workers{0};
        std::atomic<uint64_t> idle_worker_us{0};
        std::atomic<int> idle_samples{0};
        const int64_t max_workers = static_cast<int64_t>(scheduler.concurrency());

        scheduler.for_each_index(level.size(), [&](std::size_t level_index) {
            const int prev = active_parallel_workers.fetch_add(1, std::memory_order_relaxed);
            const int current = prev + 1;

            if (parent_counters) {
                uint64_t peak = parent_counters->tbb_active_workers_peak.load(std::memory_order_relaxed);
                const uint64_t current_u64 = static_cast<uint64_t>(current);
                while (peak < current_u64 &&
                       !parent_counters->tbb_active_workers_peak.compare_exchange_weak(
                           peak, current_u64, std::memory_order_relaxed)) {}

                parent_counters->tbb_active_workers_avg_sum.fetch_add(
                    current_u64, std::memory_order_relaxed);
                parent_counters->tbb_active_workers_avg_count.fetch_add(
                    1, std::memory_order_relaxed);

                const int idle_now = static_cast<int>(max_workers - current);
                if (idle_now > 0) {
                    idle_worker_us.fetch_add(
                        static_cast<uint64_t>(idle_now),
                        std::memory_order_relaxed);
                    idle_samples.fetch_add(1, std::memory_order_relaxed);
                }
            }

            execute_single_node(
                state,
                graph,
                ctx,
                level_resolved,
                level[level_index],
                level_index,
                parent_counters,
                parent_pool,
                consumer_remaining,
                &timings.cache[level_index],
                &timings.dirty[level_index],
                &timings.telemetry[level_index],
                &timings.execute[level_index],
                &timings.predicted_bbox[level_index],
                &timings.clone_context[level_index],
                &timings.state[level_index],
                compiled
            );

            active_parallel_workers.fetch_sub(1, std::memory_order_relaxed);
        });

        if (parent_counters) {
            const uint64_t idle_sum = idle_worker_us.load(std::memory_order_relaxed);
            if (idle_sum > 0) {
                parent_counters->parallel_idle_worker_entry_sum.fetch_add(
                    idle_sum, std::memory_order_relaxed);
                parent_counters->parallel_idle_worker_samples.fetch_add(
                    idle_samples.load(std::memory_order_relaxed),
                    std::memory_order_relaxed);
            }
        }
        return;
    }

    const auto t_seq0 = profiling::now();
    for (size_t level_index = 0; level_index < level.size(); ++level_index) {
        execute_single_node(
            state,
            graph,
            ctx,
            level_resolved,
            level[level_index],
            level_index,
            parent_counters,
            parent_pool,
            consumer_remaining,
            &timings.cache[level_index],
            &timings.dirty[level_index],
            &timings.telemetry[level_index],
            &timings.execute[level_index],
            &timings.predicted_bbox[level_index],
            &timings.clone_context[level_index],
            &timings.state[level_index],
            compiled
        );
    }
    if (parent_counters) {
        const auto seq_ms = profiling::duration_ms(t_seq0, profiling::now());
        parent_counters->sequential_level_execute_wall_ms.fetch_add(
            static_cast<uint64_t>(std::llround(seq_ms)),
            std::memory_order_relaxed);
    }
}

} // namespace
