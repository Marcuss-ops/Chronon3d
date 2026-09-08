// Render-plan JSON/file adapters. Included by sdk_render_engine.cpp so the
// private PIMPL remains defined in exactly one translation unit.
RenderEngine::compile_plan_json(std::string_view json) {
    std::lock_guard lock(m_impl->state_mutex);
    try {
        const auto root = nlohmann::json::parse(json.begin(), json.end());
        const auto decoded = chronon3d::render_plan::decode_render_plan(root);
        if (!decoded) {
            return RenderError{RenderErrorCode::InvalidPlan,
                               decoded.error().message, "render_plan"};
        }
        chronon3d::compiler::CompileOptions compile_options;
        compile_options.fingerprint.render_settings.width = decoded->canvas.width;
        compile_options.fingerprint.render_settings.height = decoded->canvas.height;
        compile_options.fingerprint.render_settings.deterministic = false;
        compile_options.fingerprint.render_settings.force_scalar_normal_blend = false;
        auto compiled = chronon3d::compiler::compile(
            decoded.value(), m_impl->resolver, compile_options);
        if (!compiled) {
            return RenderError{RenderErrorCode::InvalidPlan,
                               compiled.error().message, "render_plan"};
        }
        RenderSettings settings = m_impl->settings;
        settings.width = decoded->canvas.width;
        settings.height = decoded->canvas.height;
        m_impl->settings = settings;
        m_impl->prepared_job.reset();
        m_impl->prepared_composition = nullptr;
        m_impl->engine.set_settings(convert_settings(settings));
        return std::make_shared<const chronon3d::compiler::CompiledProgram>(
            std::move(compiled).value());
    } catch (const std::exception& error) {
        return RenderError{RenderErrorCode::DecodeFailure,
            std::string{"render plan JSON parse failed: "} + error.what(),
            "render_plan"};
    }
}

chronon3d::Result<RenderReport, RenderError>
RenderEngine::render_plan_file(const std::filesystem::path& plan_path,
                               std::filesystem::path output_path,
                               std::filesystem::path assets_root) {
    std::ifstream input(plan_path);
    if (!input) {
        return RenderError{RenderErrorCode::DecodeFailure,
                           "cannot open render plan: " + plan_path.string(),
                           "render_plan"};
    }
    std::ostringstream contents;
    contents << input.rdbuf();

    if (!assets_root.empty()) {
        set_assets_root(std::move(assets_root));
    }

    std::shared_ptr<const chronon3d::compiler::CompiledProgram> program_holder;
    RenderFileRequest request;
    {
        std::lock_guard lock(m_impl->state_mutex);
        try {
            const auto root = nlohmann::json::parse(contents.str());
            const auto decoded = chronon3d::render_plan::decode_render_plan(root);
            if (!decoded) {
                return RenderError{RenderErrorCode::InvalidPlan,
                                   decoded.error().message, "render_plan"};
            }
            chronon3d::render_plan::RenderPlanFingerprintOptions fingerprint_options;
            fingerprint_options.render_settings.width = decoded->canvas.width;
            fingerprint_options.render_settings.height = decoded->canvas.height;
            fingerprint_options.render_settings.deterministic = false;
            fingerprint_options.render_settings.force_scalar_normal_blend = false;
            chronon3d::compiler::CompileOptions compile_options;
            compile_options.fingerprint = fingerprint_options;
            auto compiled = chronon3d::compiler::compile(
                decoded.value(), m_impl->resolver, compile_options);
            if (!compiled) {
                return RenderError{RenderErrorCode::InvalidPlan,
                                   compiled.error().message, "render_plan"};
            }
            program_holder = std::make_shared<const chronon3d::compiler::CompiledProgram>(
                std::move(compiled).value());
            const auto& meta = program_holder->metadata();
            RenderSettings settings = m_impl->settings;
            settings.width = static_cast<int>(meta.width);
            settings.height = static_cast<int>(meta.height);
            m_impl->settings = settings;
            m_impl->prepared_job.reset();
            m_impl->prepared_composition = nullptr;
            m_impl->engine.set_settings(convert_settings(settings));

            const auto fps_num = meta.fps_numerator > 0 ? meta.fps_numerator : 30;
            const auto fps_den = meta.fps_denominator > 0 ? meta.fps_denominator : 1;
            const auto duration = meta.duration_frames;

            request.program = program_holder.get();
            request.output_path = output_path.empty()
                ? std::filesystem::path{meta.output.path}
                : output_path;
            request.start_frame = Frame{0};
            request.end_frame = Frame{duration > 0 ? duration - 1 : 0};
            request.frame_rate = chronon3d::sdk::FrameRate{fps_num, fps_den};
        } catch (const std::exception& error) {
            return RenderError{RenderErrorCode::DecodeFailure,
                std::string{"render plan error: "} + error.what(),
                "render_plan"};
        }
    }

    return render_to_file(request, {});
}
