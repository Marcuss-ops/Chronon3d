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
        chronon3d::render_plan::RenderPlanFingerprintOptions fingerprint_options;
        fingerprint_options.render_settings.width = decoded->canvas.width;
        fingerprint_options.render_settings.height = decoded->canvas.height;
        fingerprint_options.render_settings.deterministic = false;
        fingerprint_options.render_settings.force_scalar_normal_blend = false;
        auto compiled = chronon3d::render_plan::compile_render_plan(
            decoded.value(), m_impl->resolver, fingerprint_options);
        if (!compiled) {
            return RenderError{RenderErrorCode::InvalidPlan,
                               compiled.error().message, "render_plan"};
        }
        auto prepared = std::move(compiled).value();
        // Apply the plan canvas to the engine output size so subsequent
        // render_compiled()/render_to_file() render at native resolution.
        RenderSettings settings = m_impl->settings;
        settings.width = prepared.canvas.width;
        settings.height = prepared.canvas.height;
        m_impl->settings = settings;
        m_impl->prepared_job.reset();
        m_impl->prepared_composition = nullptr;
        m_impl->engine.set_settings(convert_settings(settings));
        return std::make_shared<const chronon3d::CompiledComposition>(
            std::move(prepared.compiled_composition));
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

    std::shared_ptr<const chronon3d::CompiledComposition> compiled_holder;
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
            auto compiled = chronon3d::render_plan::compile_render_plan(
                decoded.value(), m_impl->resolver, fingerprint_options);
            if (!compiled) {
                return RenderError{RenderErrorCode::InvalidPlan,
                                   compiled.error().message, "render_plan"};
            }
            auto prepared = std::move(compiled).value();
            RenderSettings settings = m_impl->settings;
            settings.width = prepared.canvas.width;
            settings.height = prepared.canvas.height;
            m_impl->settings = settings;
            m_impl->prepared_job.reset();
            m_impl->prepared_composition = nullptr;
            m_impl->engine.set_settings(convert_settings(settings));

            const auto fps = prepared.canvas.fps.numerator > 0 ? prepared.canvas.fps : chronon3d::FrameRate{30, 1};
            const auto duration = prepared.canvas.duration.integral();
            compiled_holder =
                std::make_shared<const chronon3d::CompiledComposition>(
                    std::move(prepared.compiled_composition));

            request.compiled_composition = compiled_holder.get();
            request.output_path = output_path.empty()
                ? std::filesystem::path{prepared.output.path}
                : output_path;
            request.start_frame = Frame{0};
            request.end_frame = Frame{duration > 0 ? duration - 1 : 0};
            request.frame_rate = chronon3d::sdk::FrameRate{fps.numerator, fps.denominator};
        } catch (const std::exception& error) {
            return RenderError{RenderErrorCode::DecodeFailure,
                std::string{"render plan error: "} + error.what(),
                "render_plan"};
        }
    }

    return render_to_file(request, {});
}

