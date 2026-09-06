// PreparedRenderJob bounded evaluation/render/encode batch pipeline.
//
// P1-6 — the three stage workers are persistent threads owned by
// PreparedRenderJob::Impl::WorkerPool (declared in render_engine.cpp).
// render_frames() runs one "session": publish batch parameters, wake the
// parked workers, wait for all three stage-done flags, then reset the slot
// pool for the next batch. No thread creation/join per batch.
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

    PreparedRenderBatchResult result;
    if (count.integral() == 0) {
        // Empty batch: no session is required, the pool stays parked.
        result.ok = true;
        return result;
    }
    if (!m_impl || m_impl->finished || !m_impl->engine || !m_impl->compiled) {
        throw std::runtime_error("PreparedRenderJob is no longer executable");
    }

    auto& pool = m_impl->workers;
    pool.ensure_started();

    // ── Session start: publish batch parameters and wake the workers ──
    {
        std::lock_guard lock(pool.mutex);
        pool.batch_first = first;
        pool.batch_end = first + count;
        pool.split_evaluation = m_impl->can_split_evaluation();
        pool.session_encoder = &encoder;
        pool.evaluation_done = false;
        pool.render_done = false;
        pool.encode_done = false;
        pool.failed = false;
        pool.failed_frame = Frame{-1};
        pool.error.clear();
        pool.frames_encoded = 0;
        pool.frames_rendered = 0;
        pool.max_queue_depth = 0;
        pool.session_active = true;
    }
    pool.cv.notify_all();

    // ── Session end: wait for all three stage-done flags ──
    // Each worker sets its flag as its LAST session action after its stage
    // loop broke, so when all three are set no worker touches slot storage
    // or payloads anymore. This reproduces the guarantee the previous
    // design obtained from thread::join().
    {
        std::unique_lock lock(pool.mutex);
        pool.cv.wait(lock, [&pool] {
            return pool.evaluation_done && pool.render_done && pool.encode_done;
        });
        result.frames_rendered = pool.frames_rendered;
        result.frames_encoded = pool.frames_encoded;
        result.max_queue_depth = pool.max_queue_depth;
        result.ok = !pool.failed;
        result.failed_frame = pool.failed_frame;
        result.error = pool.error;
        pool.session_active = false;
        pool.session_encoder = nullptr;
    }

    // A failed stage may leave slot ids queued. Reset only after every
    // worker has observed the drained state so the fixed pool can be
    // reused deterministically by the next batch.
    m_impl->reset_slots();

    return result;
}
