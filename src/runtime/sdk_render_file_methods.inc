// File/video rendering adapter. Included by sdk_render_engine.cpp because
// RenderEngine::Impl is intentionally private to the SDK bridge TU.
RenderEngine::render_to_file(const RenderFileRequest& request,
                             const RenderCallbacks& callbacks) {
    std::lock_guard lock(m_impl->state_mutex);

    const auto reject = [&](RenderError error)
        -> chronon3d::Result<RenderReport, RenderError> {
        if (error.component.empty()) error.component = "render_job";
        m_impl->log(LogLevel::Error, error.component, error.message);
        return error;
    };

    if (!request.composition && !request.compiled_composition) {
        return reject(RenderError{RenderErrorCode::InvalidComposition,
                                   "RenderFileRequest requires a composition or compiled_composition"});
    }
    if (request.output_path.empty()) {
        return reject(RenderError{RenderErrorCode::InvalidSettings,
                                   "RenderFileRequest output_path must not be empty"});
    }
    if (!request.video.overwrite && std::filesystem::exists(request.output_path)) {
        return reject(RenderError{RenderErrorCode::InvalidSettings,
                                   "output already exists and overwrite is disabled"});
    }
    if (m_impl->settings.width <= 0 || m_impl->settings.height <= 0) {
        return reject(RenderError{RenderErrorCode::InvalidSettings,
                                   "RenderSettings width and height must be positive"});
    }

    const auto start = request.start_frame.integral();
    const auto end = request.end_frame.integral();
    const auto step = request.step.integral();
    if (end < start || step <= 0 || request.frame_rate.numerator <= 0 ||
        request.frame_rate.denominator <= 0) {
        return reject(RenderError{RenderErrorCode::InvalidSettings,
                                   "invalid frame range or frame rate"});
    }
    const auto total = static_cast<std::uint64_t>(((end - start) / step) + 1);
    if (request.limits.max_frames != 0 && total > request.limits.max_frames) {
        return reject(RenderError{RenderErrorCode::BudgetExceeded,
                                   "render job frame limit exceeded",
                                   "render_job"});
    }
    if (request.limits.max_width != 0 &&
        static_cast<std::uint64_t>(m_impl->settings.width) > request.limits.max_width) {
        return reject(RenderError{RenderErrorCode::BudgetExceeded,
                                   "render job width limit exceeded",
                                   "render_job"});
    }
    if (request.limits.max_height != 0 &&
        static_cast<std::uint64_t>(m_impl->settings.height) > request.limits.max_height) {
        return reject(RenderError{RenderErrorCode::BudgetExceeded,
                                   "render job height limit exceeded",
                                   "render_job"});
    }
    const auto width = static_cast<std::uint64_t>(m_impl->settings.width);
    const auto height = static_cast<std::uint64_t>(m_impl->settings.height);
    if (width != 0 && height > std::numeric_limits<std::uint64_t>::max() / width / 16) {
        return reject(RenderError{RenderErrorCode::OutOfMemory,
                                   "render job memory estimate overflow",
                                   "memory"});
    }
    const auto estimated_frame_bytes = runtime::tight_surface_bytes(
        runtime::PixelFormat::Rgba32Float, width, height);
    if (request.limits.max_memory_bytes != 0 &&
        estimated_frame_bytes > request.limits.max_memory_bytes) {
        return reject(RenderError{RenderErrorCode::BudgetExceeded,
                                   "render job memory limit exceeded",
                                   "memory"});
    }

#if CHRONON3D_ENABLE_VIDEO
    using namespace chronon3d::media::video;
    VideoSinkConfig config;
    config.stream.width = m_impl->settings.width;
    config.stream.height = m_impl->settings.height;
    config.stream.frame_rate_num = request.frame_rate.numerator;
    config.stream.frame_rate_den = request.frame_rate.denominator;
    config.stream.submitted_format = media::video::PixelFormat::RGBA8;
    config.encoder.codec = convert_codec(request.video.codec);
    config.encoder.rate_control_mode = static_cast<media::video::RateControlMode>(
        request.video.rate_control_mode);
    config.encoder.bitrate = request.video.bitrate;
    config.encoder.crf = request.video.crf;
    config.encoder.qp = request.video.qp;
    const auto temp_path = request.output_path.string() + ".chronon.tmp." +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
        request.output_path.extension().string();
    config.output.output_path = temp_path;
    config.output.container = convert_container(request.video.container, request.output_path);
    config.output.overwrite = request.video.overwrite;
    config.label = "sdk::RenderEngine::render_to_file";

    auto sink = create_video_sink(config);
    if (!sink || !sink->open(config)) {
        const auto message = sink ? sink->last_error_message()
                                  : std::string{"video sink unavailable"};
        return reject(RenderError{RenderErrorCode::BackendUnavailable,
                                   message.empty() ? "failed to open video sink" : message,
                                   "encoder"});
    }

    const auto started = std::chrono::steady_clock::now();
    const auto total_frames = ((end - start) / step) + 1;

    auto fail = [&](RenderError error) -> chronon3d::Result<RenderReport, RenderError> {
        sink->close();
        std::error_code cleanup_error;
        std::filesystem::remove(temp_path, cleanup_error);
        if (error.component.empty()) error.component = "render_job";
        m_impl->log(LogLevel::Error, error.component, error.message);
        return error;
    };

    // Compile authoring input once before entering the frame loop. Runtime
    // execution below is intentionally compiled-only, so a multi-frame file
    // render cannot rebuild scene/camera state for every frame.
    std::optional<chronon3d::CompiledComposition> local_compiled;
    const chronon3d::CompiledComposition* active_compiled =
        request.compiled_composition;
    if (active_compiled == nullptr) {
        auto compiled = chronon3d::compile_composition(
            *request.composition, chronon3d::CompositionCompileContext{});
        if (!compiled) {
            return fail(RenderError{
                RenderErrorCode::InvalidComposition,
                "composition compilation failed: " + compiled.error().message});
        }
        local_compiled.emplace(std::move(compiled).value());
        active_compiled = &*local_compiled;
    }

    std::vector<std::uint8_t> pixels;
    std::uint64_t rendered = 0;

    for (std::int64_t frame = start; frame <= end; frame += step) {
        if (callbacks.is_cancelled && callbacks.is_cancelled()) {
            return fail(RenderError{RenderErrorCode::Cancelled,
                                    "render cancelled by callback"});
        }

        auto settings = convert_settings(m_impl->settings);
        settings.render_budget = active_compiled->render_budget;
        m_impl->prepared_job.reset();
        m_impl->prepared_composition = nullptr;
        m_impl->engine.set_settings(settings);
        auto framebuffer = m_impl->engine.render_compiled(
            *active_compiled, chronon3d::Frame{frame});
        if (const auto error = m_impl->engine.last_render_error()) {
            return fail(runtime_error(error->message.empty()
                ? "internal render graph reported a frame error" : error->message));
        }
        if (!framebuffer) {
            return fail(runtime_error("internal renderer returned a null framebuffer"));
        }

        framebuffer_to_rgba8(*framebuffer, pixels);
        VideoFrameView view;
        view.data = pixels.data();
        view.stride_bytes = static_cast<std::size_t>(config.stream.width) * 4;
        view.width = config.stream.width;
        view.height = config.stream.height;
        view.pixel_format = media::video::PixelFormat::RGBA8;
        view.pts = static_cast<std::int64_t>(rendered);
        if (!sink->submit(view)) {
            const auto message = sink->last_error_message();
            return fail(RenderError{RenderErrorCode::RuntimeFailure,
                message.empty() ? "video sink rejected a rendered frame" : message});
        }

        ++rendered;
        if (callbacks.progress) {
            callbacks.progress(Frame{frame}, Frame{total_frames});
        }
        if (end - frame < step) break;
    }

    if (!sink->flush() || !sink->close()) {
        const auto message = sink->last_error_message();
        std::error_code cleanup_error;
        std::filesystem::remove(temp_path, cleanup_error);
        return RenderError{RenderErrorCode::RuntimeFailure,
            message.empty() ? "video sink failed while finalizing output" : message};
    }

    std::error_code publish_error;
    std::filesystem::rename(temp_path, request.output_path, publish_error);
    if (publish_error) {
        std::error_code cleanup_error;
        std::filesystem::remove(temp_path, cleanup_error);
        return RenderError{RenderErrorCode::RuntimeFailure,
            "cannot publish output atomically: " + publish_error.message()};
    }

    return RenderReport{
        .output_path = request.output_path,
        .rendered_frames = rendered,
        .elapsed_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started).count(),
    };
#else
    return reject(RenderError{RenderErrorCode::BackendUnavailable,
                               "Built without CHRONON3D_ENABLE_VIDEO support.",
                               "encoder"});
#endif
}

chronon3d::Result<std::shared_ptr<const chronon3d::CompiledComposition>,
                  RenderError>
