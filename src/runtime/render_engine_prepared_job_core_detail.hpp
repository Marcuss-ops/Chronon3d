// PreparedRenderJob evaluation/render helpers and single-frame execution.
bool PreparedRenderJob::Impl::can_split_evaluation() const noexcept {
    if (!engine || !engine->m_impl || !engine->m_impl->m_renderer) return false;
    const auto& settings = engine->m_impl->m_renderer->render_settings();
    return settings.ssaa_factor <= 1.0f &&
        settings.motion_blur.mode == MotionBlurMode::Off;
}

EvaluatedCompositionFrame PreparedRenderJob::Impl::evaluate_frame(
    Frame frame, FrameArena& arena) const {
    if (!compiled || !compiled->composition) {
        throw std::runtime_error("PreparedRenderJob has no compiled definition");
    }
    const auto& spec = compiled->composition->spec();
    const auto& runtime = engine->m_impl->m_renderer->runtime();
    const FrameContextParams context_params{
        .global_time = SampleTime::from_frame_int(frame, spec.frame_rate),
        .duration = spec.duration,
        .width = spec.width,
        .height = spec.height,
        .assets_root = runtime.resolver().mount_root().string(),
        .resource = arena.resource(),
        .shape_registry = &engine->m_impl->m_shape_registry,
        .font_engine = &runtime.font_engine(),
        .runtime = &runtime,
    };
    const auto context = make_frame_context(context_params);
    auto evaluated = chronon3d::evaluate(
        *compiled,
        CompositionEvaluateContext{.frame_context = context},
        frame);
    if (!evaluated) {
        throw std::runtime_error(
            "prepared frame evaluation failed: " + evaluated.error().message);
    }
    return std::move(evaluated).value();
}

std::shared_ptr<Framebuffer> PreparedRenderJob::Impl::render_evaluated_frame(
    EvaluatedCompositionFrame& evaluated, Frame frame) const {
    if (evaluated.camera.has_value()) {
        evaluated.scene.set_camera_2_5d(*evaluated.camera);
    }
    return engine->m_impl->m_pipeline->render_evaluated_composition(
        *compiled, evaluated, frame);
}

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
    snapshot.pipeline_depth = m_impl->slot_pool.capacity();
    snapshot.pipeline_in_flight = m_impl->slot_pool.busy_count();
    return snapshot;
}

std::shared_ptr<Framebuffer> PreparedRenderJob::render(Frame frame) {
    if (!m_impl || m_impl->finished || !m_impl->engine || !m_impl->compiled) {
        throw std::runtime_error("PreparedRenderJob is no longer executable");
    }
    auto* evaluated = m_impl->acquire_for_evaluation();
    if (!evaluated) {
        throw std::runtime_error("PreparedRenderJob pipeline has no free frame slot");
    }
    evaluated->frame_index = frame.integral();
    auto& evaluated_payload = m_impl->payload(*evaluated);
    evaluated_payload.reset();
    if (!m_impl->publish_evaluated(*evaluated)) {
        (void)m_impl->abort(*evaluated);
        throw std::runtime_error("PreparedRenderJob failed to publish evaluated frame");
    }

    auto* render_slot = m_impl->acquire_for_render();
    if (!render_slot) {
        (void)m_impl->abort(*evaluated);
        throw std::runtime_error("PreparedRenderJob failed to acquire render slot");
    }
    try {
        std::shared_ptr<Framebuffer> output;
        const auto& settings = m_impl->engine->m_impl->m_renderer->render_settings();
        const bool can_split_evaluation =
            settings.ssaa_factor <= 1.0f &&
            settings.motion_blur.mode == MotionBlurMode::Off;
        if (can_split_evaluation && m_impl->compiled->composition) {
            auto evaluated_frame = m_impl->evaluate_frame(
                frame, evaluated_payload.arena);
            output = m_impl->engine->m_impl->m_pipeline->render_evaluated_composition(
                *m_impl->compiled, evaluated_frame, frame);
        } else {
            // Temporal accumulation and SSAA remain on the canonical complete
            // compositor until their evaluated-frame boundaries are split.
            output = m_impl->engine->render_compiled(*m_impl->compiled, frame);
        }
        if (!m_impl->publish_rendered(*render_slot)) {
            (void)m_impl->abort(*render_slot);
            throw std::runtime_error("PreparedRenderJob failed to publish rendered frame");
        }
        auto* encode_slot = m_impl->acquire_for_encoding();
        if (!encode_slot || encode_slot != render_slot ||
            !m_impl->begin_encoding(*encode_slot) ||
            !m_impl->release_encoded(*encode_slot)) {
            if (encode_slot && encode_slot->state.load(std::memory_order_acquire) != runtime::FrameSlotState::Free) {
                (void)m_impl->abort(*encode_slot);
            }
            throw std::runtime_error("PreparedRenderJob failed to release encoded frame slot");
        }
        return output;
    } catch (...) {
        if (render_slot && render_slot->state.load(std::memory_order_acquire) != runtime::FrameSlotState::Free) {
            (void)m_impl->abort(*render_slot);
        }
        throw;
    }
}
