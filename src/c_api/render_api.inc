chronon_status chronon_render_frame(chronon_engine* engine, const chronon_plan* plan,
                                    uint64_t frame, chronon_frame_buffer* output) {
    if (!engine || !plan || !output)
        return set_error(engine, CHRONON_ERROR_INVALID_ARGUMENT, "invalid render arguments");
    EngineUseGuard use(engine->in_use);
    if (!use.acquired()) return CHRONON_ERROR_BUSY;
    std::memset(output, 0, sizeof(*output));
    engine->engine.set_settings(plan->settings);
    auto result = engine->engine.render_program(
        plan->program,
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

chronon_status chronon_render_frame_with_layers_and_elements(
                                                chronon_engine* engine,
                                                const chronon_plan* plan,
                                                uint64_t frame,
                                                const chronon_layer_state* states,
                                                uint64_t state_count,
                                                const chronon_element_state* elements,
                                                uint64_t element_count,
                                                chronon_frame_buffer* out_buffer) {
    if (!engine || !plan || !out_buffer)
        return set_error(engine, CHRONON_ERROR_INVALID_ARGUMENT, "invalid render arguments");
    if ((state_count != 0 && !states) || (element_count != 0 && !elements))
        return set_error(engine, CHRONON_ERROR_INVALID_ARGUMENT, "non-zero state count with NULL pointer");
    if (state_count > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max()) ||
        element_count > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max()))
        return set_error(engine, CHRONON_ERROR_INVALID_ARGUMENT, "state count exceeds addressable size");
    chronon3d::motion::ContractApplyReport layer_report{};
    chronon3d::motion::ElementApplyReport element_report{};
    if (!chronon3d::motion::validate_layer_states(states, static_cast<std::size_t>(state_count), &layer_report) ||
        !chronon3d::motion::validate_element_states(elements, static_cast<std::size_t>(element_count), &element_report))
        return set_error(engine, CHRONON_ERROR_ABI_MISMATCH, "element or layer state record set rejected");
    EngineUseGuard use(engine->in_use);
    if (!use.acquired()) return CHRONON_ERROR_BUSY;
    std::memset(out_buffer, 0, sizeof(*out_buffer));
    engine->engine.set_settings(plan->settings);
    auto result = engine->engine.render_program(
        plan->program, chronon3d::sdk::Frame{static_cast<std::int64_t>(frame)},
        states, static_cast<std::size_t>(state_count),
        elements, static_cast<std::size_t>(element_count));
    if (!result) return set_render_error(engine, result.error());
    const auto& rendered = result.value();
    const auto size = rendered_byte_size(rendered);
    if (!size) return set_error(engine, CHRONON_ERROR_IO_FAILED, "invalid rendered frame size");
    auto* data = static_cast<std::uint8_t*>(std::realloc(engine->buffer, *size));
    if (!data) return set_error(engine, CHRONON_ERROR_IO_FAILED, "frame buffer allocation failed");
    engine->buffer = data;
    engine->buffer_size = *size;
    std::memcpy(engine->buffer, rendered.pixels, *size);
    out_buffer->data = engine->buffer;
    out_buffer->size = *size;
    out_buffer->width = static_cast<uint32_t>(rendered.width);
    out_buffer->height = static_cast<uint32_t>(rendered.height);
    out_buffer->stride = static_cast<uint32_t>(rendered.bytes_per_row);
    out_buffer->pixel_format = static_cast<uint32_t>(rendered.format);
    clear_error(engine);
    return CHRONON_OK;
}

chronon_status chronon_render_frame_with_layers(chronon_engine* engine,
                                                const chronon_plan* plan,
                                                uint64_t frame,
                                                const chronon_layer_state* states,
                                                uint64_t state_count,
                                                chronon_frame_buffer* out_buffer) {
    if (!engine || !plan || !out_buffer)
        return set_error(engine, CHRONON_ERROR_INVALID_ARGUMENT, "invalid render arguments");
    if (state_count != 0 && !states)
        return set_error(engine, CHRONON_ERROR_INVALID_ARGUMENT,
                         "layer state count is non-zero but states is NULL");
    if (state_count > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max()))
        return set_error(engine, CHRONON_ERROR_INVALID_ARGUMENT,
                         "layer state count exceeds the addressable size");

    // ADR-032 discipline before any render work: a handshake the contract
    // refuses must not quietly produce a frame that looks plausible. The
    // predicate lives in chronon3d::motion so this check and the one the
    // render path performs internally cannot drift apart.
    chronon3d::motion::ContractApplyReport refused{};
    if (!chronon3d::motion::validate_layer_states(
            states, static_cast<std::size_t>(state_count), &refused)) {
        return set_error(engine, CHRONON_ERROR_ABI_MISMATCH,
                         "ADR-032 layer state record set rejected: struct_size, "
                         "reserved (0) or out-of-range layer id, duplicate layer "
                         "id, or non-finite matrix");
    }

    EngineUseGuard use(engine->in_use);
    if (!use.acquired()) return CHRONON_ERROR_BUSY;
    std::memset(out_buffer, 0, sizeof(*out_buffer));
    engine->engine.set_settings(plan->settings);
    auto result = engine->engine.render_program(
        plan->program,
        chronon3d::sdk::Frame{static_cast<std::int64_t>(frame)},
        states, static_cast<std::size_t>(state_count));
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
    out_buffer->data = engine->buffer;
    out_buffer->size = *size;
    out_buffer->width = static_cast<uint32_t>(rendered.width);
    out_buffer->height = static_cast<uint32_t>(rendered.height);
    out_buffer->stride = static_cast<uint32_t>(rendered.bytes_per_row);
    out_buffer->pixel_format = static_cast<uint32_t>(rendered.format);
    clear_error(engine);
    return CHRONON_OK;
}

chronon_status chronon_render_frame_into(chronon_engine* engine,
                                          const chronon_plan* plan,
                                          uint64_t frame, void* destination,
                                          uint64_t destination_size,
                                          chronon_frame_info* output) {
    if (!engine || !plan || !output)
        return set_error(engine, CHRONON_ERROR_INVALID_ARGUMENT, "invalid render-into arguments");
    EngineUseGuard use(engine->in_use);
    if (!use.acquired()) return CHRONON_ERROR_BUSY;
    std::memset(output, 0, sizeof(*output));
    engine->engine.set_settings(plan->settings);
    auto result = engine->engine.render_program(
        plan->program,
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
    if (!engine || !plan || !output_path || fps_num == 0 || fps_den == 0)
        return set_error(engine, CHRONON_ERROR_INVALID_ARGUMENT, "invalid file render arguments");
    EngineUseGuard use(engine->in_use);
    if (!use.acquired()) return CHRONON_ERROR_BUSY;
    engine->engine.set_settings(plan->settings);
    chronon3d::sdk::RenderFileRequest request;
    request.program = &plan->program;
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
