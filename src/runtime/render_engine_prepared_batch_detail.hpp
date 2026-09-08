// PreparedRenderJob compatibility batch adapter. The lifecycle itself is
// owned by RenderPipeline; this public handle only validates state and maps
// the runtime result to the API result type.
PreparedRenderBatchResult PreparedRenderJob::render_frames(
    Frame first,
    Frame count,
    const PreparedFrameEncoder& encoder)
{
    if (!m_impl || !m_impl->job_session || !m_impl->pipeline ||
        !m_impl->compatibility_composition) {
        throw std::runtime_error("PreparedRenderJob is no longer executable");
    }
    if (!encoder) {
        throw std::invalid_argument("PreparedRenderJob encoder callback is empty");
    }
    if (count.integral() < 0) {
        throw std::invalid_argument("PreparedRenderJob frame count must not be negative");
    }

    PreparedRenderBatchResult result;
    const auto runtime_result = m_impl->pipeline->render_compatibility_frames(
        *m_impl->job_session, *m_impl->compatibility_composition,
        first, count, encoder);
    result.ok = runtime_result.ok;
    result.frames_rendered = runtime_result.frames_rendered;
    result.frames_encoded = runtime_result.frames_encoded;
    result.max_queue_depth = runtime_result.max_queue_depth;
    result.failed_frame = runtime_result.failed_frame;
    result.error = runtime_result.error;
    return result;
}
