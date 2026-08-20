// PreparedRenderJob evaluation/render/encode pipeline. Included by
// render_engine.cpp so the private RenderEngine::Impl stays single-source.
bool PreparedRenderJob::Impl::can_split_evaluation() const noexcept {
    if (!engine || !engine->m_impl || !engine->m_impl->m_renderer) return false;
    const auto& settings = engine->m_impl->m_renderer->render_settings();
    return settings.ssaa_factor <= 1.0f &&
        settings.motion_blur.mode == MotionBlurMode::Off;
}

EvaluatedCompositionFrame PreparedRenderJob::Impl::evaluate_frame(
    Frame frame, FrameArena& arena) const {
    if (!compiled || !compiled->definition) {
        throw std::runtime_error("PreparedRenderJob has no compiled definition");
    }
    const auto& spec = compiled->definition->composition;
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
    snapshot.pipeline_depth = m_impl->slots.depth();
    snapshot.pipeline_in_flight = m_impl->slots.in_flight();
    return snapshot;
}

std::shared_ptr<Framebuffer> PreparedRenderJob::render(Frame frame) {
    if (!m_impl || m_impl->finished || !m_impl->engine || !m_impl->compiled) {
        throw std::runtime_error("PreparedRenderJob is no longer executable");
    }
    auto* evaluated = m_impl->slots.acquire_for_evaluation();
    if (!evaluated) {
        throw std::runtime_error("PreparedRenderJob pipeline has no free frame slot");
    }
    evaluated->frame = frame;
    evaluated->sequence = m_impl->next_sequence++;
    if (!m_impl->slots.publish_evaluated(*evaluated)) {
        (void)m_impl->slots.abort(*evaluated);
        throw std::runtime_error("PreparedRenderJob failed to publish evaluated frame");
    }

    auto* render_slot = m_impl->slots.acquire_for_render();
    if (!render_slot) {
        (void)m_impl->slots.abort(*evaluated);
        throw std::runtime_error("PreparedRenderJob failed to acquire render slot");
    }
    try {
        std::shared_ptr<Framebuffer> output;
        const auto& settings = m_impl->engine->m_impl->m_renderer->render_settings();
        const bool can_split_evaluation =
            settings.ssaa_factor <= 1.0f &&
            settings.motion_blur.mode == MotionBlurMode::Off;
        if (can_split_evaluation && m_impl->compiled->definition) {
            auto evaluated_frame = m_impl->evaluate_frame(
                frame, evaluated->arena);
            output = m_impl->engine->m_impl->m_pipeline->render_evaluated_composition(
                *m_impl->compiled, evaluated_frame, frame);
        } else {
            // Temporal accumulation and SSAA remain on the canonical complete
            // compositor until their evaluated-frame boundaries are split.
            output = m_impl->engine->render_compiled(*m_impl->compiled, frame);
        }
        if (!m_impl->slots.publish_rendered(*render_slot)) {
            (void)m_impl->slots.abort(*render_slot);
            throw std::runtime_error("PreparedRenderJob failed to publish rendered frame");
        }
        auto* encode_slot = m_impl->slots.acquire_for_encoding();
        if (!encode_slot || encode_slot != render_slot ||
            !m_impl->slots.begin_encoding(*encode_slot) ||
            !m_impl->slots.release_encoded(*encode_slot)) {
            if (encode_slot && encode_slot->state != runtime::FrameSlotState::Free) {
                (void)m_impl->slots.abort(*encode_slot);
            }
            throw std::runtime_error("PreparedRenderJob failed to release encoded frame slot");
        }
        return output;
    } catch (...) {
        if (render_slot->state != runtime::FrameSlotState::Free) {
            (void)m_impl->slots.abort(*render_slot);
        }
        throw;
    }
}

PreparedRenderBatchResult PreparedRenderJob::render_frames(
    Frame first,
    Frame count,
    const PreparedFrameEncoder& encoder)
{
    if (!encoder) {
        throw std::invalid_argument("PreparedRenderJob encoder callback is empty");
    }
    if (count.integral() < 0) {
        throw std::invalid_argument("PreparedRenderJob frame count must not be negative");
    }

    // The three stage queues are owned by FrameSlotPipeline.  Payloads live
    // in the fixed slots themselves, so a slow encoder cannot cause a second,
    // unbounded payload queue to grow.
    std::mutex wait_mutex;
    std::condition_variable stage_changed;
    bool evaluation_done = false;
    bool render_done = false;
    bool failed = false;
    Frame failed_frame{-1};
    std::string error;
    std::size_t frames_encoded = 0;
    std::size_t frames_rendered = 0;
    std::size_t max_queue_depth = 0;

    auto fail = [&](Frame frame, std::string message) {
        {
            std::lock_guard lock(wait_mutex);
            failed = true;
            failed_frame = frame;
            if (error.empty()) error = std::move(message);
        }
        stage_changed.notify_all();
    };

    const auto slot_index = [&](const runtime::FrameSlot* slot) {
        return static_cast<std::size_t>(
            slot - m_impl->slots.slots().data());
    };
    const bool split_evaluation = m_impl->can_split_evaluation();
    const Frame end = first + count;

    std::thread evaluation_thread([&] {
        for (Frame frame = first; frame < end; ++frame) {
            runtime::FrameSlot* slot = nullptr;
            {
                std::unique_lock lock(wait_mutex);
                stage_changed.wait(lock, [&] {
                    return failed || (slot = m_impl->slots.acquire_for_evaluation()) != nullptr;
                });
                if (failed) break;
            }
            const auto index = slot_index(slot);
            slot->frame = frame;
            slot->sequence = m_impl->next_sequence++;
            try {
                if (split_evaluation) {
                    slot->evaluated.emplace(
                        m_impl->evaluate_frame(frame, slot->arena));
                }
                if (!m_impl->slots.publish_evaluated(*slot)) {
                    throw std::runtime_error("PreparedRenderJob failed to publish evaluated frame");
                }
            } catch (const std::exception& exception) {
                (void)m_impl->slots.abort(*slot);
                fail(frame, std::string{"PreparedRenderJob evaluation failed: "} + exception.what());
                break;
            } catch (...) {
                (void)m_impl->slots.abort(*slot);
                fail(frame, "PreparedRenderJob evaluation failed with an unknown exception");
                break;
            }
            stage_changed.notify_all();
        }
        {
            std::lock_guard lock(wait_mutex);
            evaluation_done = true;
        }
        stage_changed.notify_all();
    });

    std::thread render_thread([&] {
        for (;;) {
            runtime::FrameSlot* slot = nullptr;
            {
                std::unique_lock lock(wait_mutex);
                stage_changed.wait(lock, [&] {
                    if (slot) return true;
                    if ((slot = m_impl->slots.acquire_for_render()) != nullptr) return true;
                    return failed || evaluation_done;
                });
                if (!slot) {
                    if (failed || evaluation_done) {
                        lock.unlock();
                        {
                            std::lock_guard done_lock(wait_mutex);
                            render_done = true;
                        }
                        stage_changed.notify_all();
                        return;
                    }
                    continue;
                }
            }
            const auto index = slot_index(slot);
            const Frame slot_frame = slot->frame;
            try {
                std::shared_ptr<Framebuffer> output;
                if (split_evaluation) {
                    output = m_impl->render_evaluated_frame(
                        *slot->evaluated, slot_frame);
                    slot->evaluated.reset();
                } else {
                    // Temporal/SSAA paths retain their canonical complete
                    // compositor boundary, but still participate in the
                    // bounded render→encode stages without recursively
                    // acquiring the same slot pipeline.
                    output = m_impl->engine->render_compiled(
                        *m_impl->compiled, slot_frame);
                }
                slot->rendered = std::move(output);
                ++frames_rendered;
                if (!m_impl->slots.publish_rendered(*slot)) {
                    throw std::runtime_error("PreparedRenderJob failed to publish rendered frame");
                }
                if (m_impl->slots.rendered_depth() > max_queue_depth) {
                    max_queue_depth = m_impl->slots.rendered_depth();
                }
            } catch (const std::exception& exception) {
                (void)m_impl->slots.abort(*slot);
                fail(slot_frame, std::string{"PreparedRenderJob render failed: "} + exception.what());
                return;
            } catch (...) {
                (void)m_impl->slots.abort(*slot);
                fail(slot_frame, "PreparedRenderJob render failed with an unknown exception");
                return;
            }
            stage_changed.notify_all();
        }
    });

    std::thread encoder_thread([&] {
        for (;;) {
            runtime::FrameSlot* slot = nullptr;
            {
                std::unique_lock lock(wait_mutex);
                stage_changed.wait(lock, [&] {
                    if (slot) return true;
                    if ((slot = m_impl->slots.acquire_for_encoding()) != nullptr) return true;
                    return failed || render_done;
                });
                if (!slot) {
                    if (failed || render_done) return;
                    continue;
                }
            }
            const auto index = slot_index(slot);
            const Frame slot_frame = slot->frame;

            try {
                if (!m_impl->slots.begin_encoding(*slot)) {
                    throw std::runtime_error(
                        "PreparedRenderJob failed to begin encoding frame");
                }
                if (!slot->rendered ||
                    !encoder(slot_frame, *slot->rendered)) {
                    (void)m_impl->slots.abort(*slot);
                    fail(slot_frame, "PreparedRenderJob encoder rejected frame");
                    return;
                }
            } catch (const std::exception& exception) {
                (void)m_impl->slots.abort(*slot);
                fail(slot_frame, std::string{"PreparedRenderJob encoder threw: "} + exception.what());
                return;
            } catch (...) {
                (void)m_impl->slots.abort(*slot);
                fail(slot_frame, "PreparedRenderJob encoder threw an unknown exception");
                return;
            }
            slot->rendered.reset();
            (void)m_impl->slots.release_encoded(*slot);
            ++frames_encoded;
            stage_changed.notify_all();
        }
    });

    evaluation_thread.join();
    render_thread.join();
    encoder_thread.join();

    // A failed stage may have left payloads in one of the two internal rings.
    // Reset only after every worker has joined; this returns every fixed slot
    // and makes the PreparedRenderJob reusable after an encoder failure.
    m_impl->slots.reset();

    PreparedRenderBatchResult result;
    result.frames_rendered = frames_rendered;
    result.frames_encoded = frames_encoded;
    result.max_queue_depth = max_queue_depth;
    {
        std::lock_guard lock(wait_mutex);
        result.ok = !failed;
        result.failed_frame = failed_frame;
        result.error = error;
    }
    return result;
}
