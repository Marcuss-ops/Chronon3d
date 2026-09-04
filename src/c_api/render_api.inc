chronon_status chronon_render_frame(chronon_engine* engine, const chronon_plan* plan,
                                    uint64_t frame, chronon_frame_buffer* output) {
    if (!engine || !plan || !plan->prepared.compiled_composition.composition || !output)
        return set_error(engine, CHRONON_ERROR_INVALID_ARGUMENT, "invalid render arguments");
    EngineUseGuard use(engine->in_use);
    if (!use.acquired()) return CHRONON_ERROR_BUSY;
    std::memset(output, 0, sizeof(*output));
    engine->engine.set_settings(plan->settings);
    auto result = engine->engine.render_compiled(
        plan->prepared.compiled_composition,
        chronon3d::sdk::Frame{static_cast<std::int64_t>(frame)});
    if (!result) return set_render_error(engine, result.error());
    const auto& rendered = result.value();
    const auto size = rendered_byte_size(rendered);
    if (!size) return set_error(engine, CHRONON_ERROR_IO_FAILED,
                                "rendered frame size overflow or invalid dimensions");
    auto* data = static_cast<std::uint8_t*>(std::realloc(engine->buffer, *size));
    if (!data) return set_error(engine, CHRONON_ERROR_IO_FAILED, "frame buffer allocation failed");
    engine->buffer = data;
    engine->buffer_size = *size;
    std::memcpy(engine->buffer, rendered.pixels, *size);
    output->data = engine->buffer;
    output->size = *size;
    output->width = static_cast<uint32_t>(rendered.width);
    output->height = static_cast<uint32_t>(rendered.height);
    output->stride = static_cast<uint32_t>(rendered.bytes_per_row);
    output->pixel_format = static_cast<uint32_t>(rendered.format);
    clear_error(engine);
    return CHRONON_OK;
}

chronon_status chronon_render_frame_into(chronon_engine* engine,
                                          const chronon_plan* plan,
                                          uint64_t frame, void* destination,
                                          uint64_t destination_size,
                                          chronon_frame_info* output) {
    if (!engine || !plan || !plan->prepared.compiled_composition.composition || !output)
        return set_error(engine, CHRONON_ERROR_INVALID_ARGUMENT, "invalid render-into arguments");
    EngineUseGuard use(engine->in_use);
    if (!use.acquired()) return CHRONON_ERROR_BUSY;
    std::memset(output, 0, sizeof(*output));
    engine->engine.set_settings(plan->settings);
    auto result = engine->engine.render_compiled(
        plan->prepared.compiled_composition,
        chronon3d::sdk::Frame{static_cast<std::int64_t>(frame)});
    if (!result) return set_render_error(engine, result.error());
    const auto& rendered = result.value();
    const auto size = rendered_byte_size(rendered);
    if (!size) return set_error(engine, CHRONON_ERROR_IO_FAILED,
                                "rendered frame size overflow or invalid dimensions");
    output->size = static_cast<uint64_t>(*size);
    output->width = static_cast<uint32_t>(rendered.width);
    output->height = static_cast<uint32_t>(rendered.height);
    output->stride = static_cast<uint32_t>(rendered.bytes_per_row);
    output->pixel_format = static_cast<uint32_t>(rendered.format);
    if (!destination || destination_size < *size)
        return set_error(engine, CHRONON_ERROR_BUFFER_TOO_SMALL,
                         "destination buffer is too small; query required size in out_info->size");
    std::memcpy(destination, rendered.pixels, *size);
    clear_error(engine);
    return CHRONON_OK;
}

chronon_status chronon_render_file(chronon_engine* engine, const chronon_plan* plan,
                                   const char* output_path, uint64_t start_frame,
                                   uint64_t end_frame, uint32_t fps_num,
                                   uint32_t fps_den, const chronon_render_callbacks* cb) {
    if (!engine || !plan || !plan->prepared.compiled_composition.composition ||
        !output_path || fps_num == 0 || fps_den == 0)
        return set_error(engine, CHRONON_ERROR_INVALID_ARGUMENT, "invalid file render arguments");
    EngineUseGuard use(engine->in_use);
    if (!use.acquired()) return CHRONON_ERROR_BUSY;
    engine->engine.set_settings(plan->settings);
    chronon3d::sdk::RenderFileRequest request;
    request.compiled_composition = &plan->prepared.compiled_composition;
    request.output_path = output_path;
    request.start_frame = {static_cast<std::int64_t>(start_frame)};
    request.end_frame = {static_cast<std::int64_t>(end_frame)};
    request.frame_rate = {static_cast<std::int32_t>(fps_num), static_cast<std::int32_t>(fps_den)};
    chronon3d::sdk::RenderCallbacks callbacks;
    if (cb && cb->progress) callbacks.progress = [cb](auto current, auto total) {
        cb->progress(cb->user, static_cast<uint64_t>(current.integral()),
                     static_cast<uint64_t>(total.integral()));
    };
    if (cb && cb->is_cancelled)
        callbacks.is_cancelled = [cb] { return cb->is_cancelled(cb->user) != 0; };
    auto result = engine->engine.render_to_file(request, callbacks);
    if (!result) return set_render_error(engine, result.error());
    clear_error(engine);
    return CHRONON_OK;
}
