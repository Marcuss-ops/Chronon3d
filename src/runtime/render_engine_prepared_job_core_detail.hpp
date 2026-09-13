PreparedRenderJobTelemetry PreparedRenderJob::telemetry() const noexcept {
    PreparedRenderJobTelemetry snapshot;
    if (!m_impl || !m_impl->engine || !m_impl->engine->m_impl ||
        !m_impl->engine->m_impl->m_renderer) {
        return snapshot;
    }
    const auto& renderer = *m_impl->engine->m_impl->m_renderer;
    const auto cache_stats = renderer.node_cache().stats();
    snapshot.cache_hits = renderer.counters()->cache_hits.load(std::memory_order_relaxed);
    snapshot.cache_misses = renderer.counters()->cache_misses.load(std::memory_order_relaxed);
    snapshot.cache_evictions = cache_stats.evictions;
    snapshot.nodes_executed = renderer.counters()->nodes_executed.load(std::memory_order_relaxed);
    snapshot.nodes_skipped = renderer.counters()->nodes_skipped.load(std::memory_order_relaxed);
    snapshot.framebuffer_allocations = renderer.counters()->framebuffer_allocations.load(
        std::memory_order_relaxed);
    snapshot.framebuffer_bytes_allocated = renderer.counters()->framebuffer_bytes_allocated.load(
        std::memory_order_relaxed);
    snapshot.cache_entries = cache_stats.current_size;
    snapshot.cache_bytes = cache_stats.current_weight;
    snapshot.cache_capacity_bytes = renderer.node_cache().capacity();
    snapshot.pipeline_depth = m_impl->job_session
        ? m_impl->job_session->slots().capacity() : 0;
    snapshot.pipeline_in_flight = m_impl->job_session
        ? m_impl->job_session->slots().busy_count() : 0;
    return snapshot;
}

std::shared_ptr<Framebuffer> PreparedRenderJob::render(Frame frame) {
    if (!m_impl || m_impl->finished || !m_impl->engine || !m_impl->job_session ||
        (!m_impl->job_session->program_ptr() &&
         !m_impl->compatibility_composition)) {
        throw std::runtime_error("PreparedRenderJob is no longer executable");
    }
    auto result = m_impl->job_session->program_ptr()
        ? m_impl->pipeline->render(*m_impl->job_session, frame)
        : m_impl->pipeline->render_compatibility(
            *m_impl->job_session, *m_impl->compatibility_composition, frame);
    if (!result) {
        // This adapter keeps its throw contract for OPP callers, while the
        // typed scheduling code stays available to direct pipeline callers
        // that must distinguish recoverable states without string matching.
        // The class is stated explicitly because it answers the only question
        // the caller has: backpressure is retryable, anything else is not.
        const auto code = result.error();
        throw std::runtime_error(
            std::string{"RenderPipeline scheduling failed: "} +
            runtime::frame_scheduling_class_name(
                runtime::frame_scheduling_class(code)) +
            " (" + runtime::frame_scheduling_code_name(code) + ", retryable=" +
            (runtime::frame_scheduling_is_retryable(code) ? "yes" : "no") +
            ")");
    }
    return std::move(result).value();
}
