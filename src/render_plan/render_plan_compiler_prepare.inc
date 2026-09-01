        const auto compile_total_t0 = chronon3d::profiling::now();
        double asset_prepare_ms = 0.0;
        double fingerprint_ms = 0.0;
        double media_resolution_ms = 0.0;
        double materialize_layout_ms = 0.0;
        double composition_compile_ms = 0.0;
        if (const auto budget_error = validate_render_budget(plan)) {
            return *budget_error;
        }
        const auto asset_t0 = chronon3d::profiling::now();
        auto prepared_store = chronon3d::assets::prepare_asset_store(plan, resolver);
        if (!prepared_store) {
            return PlanDecodeError{
                "assets." + prepared_store.error().logical_path,
                prepared_store.error().message,
                prepared_store.error().code ==
                        chronon3d::assets::AssetPreflightErrorCode::MissingAsset
                    ? "MissingAsset" : "",
                "asset_resolver"};
        }
        auto resources = std::move(prepared_store).value();
        asset_prepare_ms = chronon3d::profiling::duration_ms(
            asset_t0, chronon3d::profiling::now());
        auto assets = resources.manifest();
        const auto fingerprint_t0 = chronon3d::profiling::now();
        const auto fingerprints = render_job_fingerprint(plan, assets, fingerprint_options);
        auto content_plan = plan;
        content_plan.job_id.clear();
        content_plan.output = {};
        const auto content_fingerprint = compute_render_plan_content_fingerprint(content_plan);
        fingerprint_ms = chronon3d::profiling::duration_ms(
            fingerprint_t0, chronon3d::profiling::now());
        const auto canvas = CanvasInfo::from_dimensions(
            static_cast<float>(plan.canvas.width),
            static_cast<float>(plan.canvas.height));
        std::vector<std::optional<presets::text::SubtitleTrack>> subtitles(plan.layers.size());
        for (std::size_t index = 0; index < plan.layers.size(); ++index) {
            const auto& layer = plan.layers[index];
            if (layer.type != LayerType::SubtitleTrack) continue;
            const auto subtitle = resources.find(
                std::filesystem::path(layer.source).lexically_normal().generic_string(),
                chronon3d::assets::PreparedAssetKind::Subtitle);
            if (!subtitle || subtitle->bytes.empty()) {
                throw std::runtime_error("subtitle asset was not prepared: " + layer.source);
            }
            const std::string raw(
                reinterpret_cast<const char*>(subtitle->bytes.data()), subtitle->bytes.size());
            if (layer.subtitle_format == SubtitleFormat::Vtt)
                subtitles[index] = presets::text::subtitle_from_vtt(raw);
            else if (layer.subtitle_format == SubtitleFormat::Json)
                subtitles[index] = presets::text::subtitle_from_json(raw);
            else if (layer.subtitle_format == SubtitleFormat::Ass)
                subtitles[index] = presets::text::subtitle_from_ass(raw);
            else
                subtitles[index] = presets::text::subtitle_from_srt(raw);
        }

        const auto media_t0 = chronon3d::profiling::now();
        std::vector<std::string> resolved_video_paths(plan.layers.size());
        for (std::size_t index = 0; index < plan.layers.size(); ++index) {
            const auto& layer = plan.layers[index];
            if (layer.type != LayerType::Video) continue;
            const auto resolved = resolver.resolve_logical(std::filesystem::path(layer.source));
            if (!resolved) {
                throw std::runtime_error("video source was not prepared: " + layer.source);
            }
            resolved_video_paths[index] = resolved->string();
        }
        media_resolution_ms = chronon3d::profiling::duration_ms(
            media_t0, chronon3d::profiling::now());

        CompositionSpec spec;
        spec.name = plan.job_id;
        spec.width = plan.canvas.width;
        spec.height = plan.canvas.height;
        spec.frame_rate = plan.canvas.fps;
        spec.duration = plan.canvas.duration;

        auto font_resolver = std::make_shared<chronon3d::assets::AssetResolver>();
        if (const auto root = resolver.mount_root(); !root.empty()) {
            font_resolver->mount(root);
        }
        auto font_engine = std::make_shared<chronon3d::FontEngine>(*font_resolver);
