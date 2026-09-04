// PreparedRenderJob bounded evaluation/render/encode batch pipeline.
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

    // PreparedRenderJob orchestrates the three stages directly. Slot storage,
    // bounded slot-id handoff, and GPU completion state remain owned by the
    // canonical FrameSlotPool, FrameQueue, and GpuCompletionTracker components.
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

    const bool split_evaluation = m_impl->can_split_evaluation();
    const Frame end = first + count;

    std::thread evaluation_thread([&] {
        for (Frame frame = first; frame < end; ++frame) {
            runtime::FrameExecutionSlot* slot = nullptr;
            {
                std::unique_lock lock(wait_mutex);
                stage_changed.wait(lock, [&] {
                    return failed || (slot = m_impl->acquire_for_evaluation()) != nullptr;
                });
                if (failed) break;
            }
            slot->frame_index = frame.integral();
            auto& payload = m_impl->payload(*slot);
            payload.reset();
            try {
                if (split_evaluation) {
                    payload.evaluated.emplace(
                        m_impl->evaluate_frame(frame, payload.arena));
                }
                if (!m_impl->publish_evaluated(*slot)) {
                    throw std::runtime_error("PreparedRenderJob failed to publish evaluated frame");
                }
            } catch (const std::exception& exception) {
                (void)m_impl->abort(*slot);
                fail(frame, std::string{"PreparedRenderJob evaluation failed: "} + exception.what());
                break;
            } catch (...) {
                (void)m_impl->abort(*slot);
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
            runtime::FrameExecutionSlot* slot = nullptr;
            {
                std::unique_lock lock(wait_mutex);
                stage_changed.wait(lock, [&] {
                    if (slot) return true;
                    if ((slot = m_impl->acquire_for_render()) != nullptr) return true;
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
            auto& payload = m_impl->payload(*slot);
            const Frame slot_frame{static_cast<std::int64_t>(slot->frame_index)};
            try {
                std::shared_ptr<Framebuffer> output;
                if (split_evaluation) {
                    output = m_impl->render_evaluated_frame(
                        *payload.evaluated, slot_frame);
                    payload.evaluated.reset();
                } else {
                    // Temporal/SSAA paths retain their canonical complete
                    // compositor boundary, but still participate in the
                    // bounded render→encode stages without recursively
                    // acquiring the same slot pipeline.
                    output = m_impl->engine->render_compiled(
                        *m_impl->compiled, slot_frame);
                }
                payload.rendered = std::move(output);
                ++frames_rendered;
                if (!m_impl->publish_rendered(*slot)) {
                    throw std::runtime_error("PreparedRenderJob failed to publish rendered frame");
                }
                if (m_impl->rendered_depth() > max_queue_depth) {
                    max_queue_depth = m_impl->rendered_depth();
                }
            } catch (const std::exception& exception) {
                (void)m_impl->abort(*slot);
                fail(slot_frame, std::string{"PreparedRenderJob render failed: "} + exception.what());
                return;
            } catch (...) {
                (void)m_impl->abort(*slot);
                fail(slot_frame, "PreparedRenderJob render failed with an unknown exception");
                return;
            }
            stage_changed.notify_all();
        }
    });

    std::thread encoder_thread([&] {
        for (;;) {
            runtime::FrameExecutionSlot* slot = nullptr;
            {
                std::unique_lock lock(wait_mutex);
                stage_changed.wait(lock, [&] {
                    if (slot) return true;
                    if ((slot = m_impl->acquire_for_encoding()) != nullptr) return true;
                    return failed || render_done;
                });
                if (!slot) {
                    if (failed || render_done) return;
                    continue;
                }
            }
            auto& payload = m_impl->payload(*slot);
            const Frame slot_frame{static_cast<std::int64_t>(slot->frame_index)};

            try {
                if (!m_impl->begin_encoding(*slot)) {
                    throw std::runtime_error(
                        "PreparedRenderJob failed to begin encoding frame");
                }
                if (!payload.rendered ||
                    !encoder(slot_frame, *payload.rendered)) {
                    (void)m_impl->abort(*slot);
                    fail(slot_frame, "PreparedRenderJob encoder rejected frame");
                    return;
                }
            } catch (const std::exception& exception) {
                (void)m_impl->abort(*slot);
                fail(slot_frame, std::string{"PreparedRenderJob encoder threw: "} + exception.what());
                return;
            } catch (...) {
                (void)m_impl->abort(*slot);
                fail(slot_frame, "PreparedRenderJob encoder threw an unknown exception");
                return;
            }
            payload.rendered.reset();
            (void)m_impl->release_encoded(*slot);
            ++frames_encoded;
            stage_changed.notify_all();
        }
    });

    evaluation_thread.join();
    render_thread.join();
    encoder_thread.join();

    // A failed stage may leave slot ids queued. Reset only after every worker
    // has joined so the fixed pool can be reused deterministically.
    m_impl->reset_slots();

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
