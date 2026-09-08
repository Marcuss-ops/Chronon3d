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
        !m_impl->job_session->legacy_composition()) {
        throw std::runtime_error("PreparedRenderJob is no longer executable");
    }
    auto* slot = m_impl->job_session->acquire_for_evaluation();
    if (!slot) {
        throw std::runtime_error("PreparedRenderJob pipeline has no free frame slot");
    }
    slot->frame_index = frame.integral();
    auto& payload = m_impl->job_session->payload(*slot);
    const auto frame_time = SampleTime::from_frame_int(
        frame, m_impl->job_session->legacy_composition()->composition
            ? m_impl->job_session->legacy_composition()->composition->frame_rate() : FrameRate{});
    runtime::FrameExecution frame_execution(
        *m_impl->job_session, frame, frame_time, *slot, payload);
    payload.reset();
    if (!m_impl->job_session->publish_evaluated(*slot)) {
        (void)m_impl->job_session->abort(*slot);
        throw std::runtime_error("PreparedRenderJob failed to publish evaluated frame");
    }

    auto* render_slot = m_impl->job_session->acquire_for_render();
    if (!render_slot) {
        (void)m_impl->job_session->abort(*slot);
        throw std::runtime_error("PreparedRenderJob failed to acquire render slot");
    }
    try {
        std::shared_ptr<Framebuffer> output;
        output = m_impl->pipeline->render_compiled_composition(
            *m_impl->job_session->legacy_composition(), frame, &frame_execution);
        if (!m_impl->job_session->publish_rendered(*render_slot)) {
            (void)m_impl->job_session->abort(*render_slot);
            throw std::runtime_error("PreparedRenderJob failed to publish rendered frame");
        }
        auto* encode_slot = m_impl->job_session->acquire_for_encoding();
        if (!encode_slot || encode_slot != render_slot ||
            !m_impl->job_session->begin_encoding(*encode_slot) ||
            !m_impl->job_session->release_encoded(*encode_slot)) {
            if (encode_slot && encode_slot->state.load(std::memory_order_acquire) != runtime::FrameSlotState::Free) {
                (void)m_impl->job_session->abort(*encode_slot);
            }
            throw std::runtime_error("PreparedRenderJob failed to release encoded frame slot");
        }
        return output;
    } catch (...) {
        frame_execution.fail();
        if (render_slot && render_slot->state.load(std::memory_order_acquire) != runtime::FrameSlotState::Free) {
            (void)m_impl->job_session->abort(*render_slot);
        }
        throw;
    }
}
