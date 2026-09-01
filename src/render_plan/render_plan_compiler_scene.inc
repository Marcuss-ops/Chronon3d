        CompositionDefinition definition;
        definition.composition = spec;
        definition.scene = [plan, canvas, subtitles, resolved_video_paths,
                            prepared_texts, prepared_image_positions,
                            prepared_images, font_resolver, font_engine](
                               const FrameContext& ctx) {
                SceneBuilder scene(ctx);
                scene.font_engine(font_engine.get());

                for (const auto& layer : plan.layers) {
                    if (!layer.background || layer.background->asset.empty()) continue;
                    scene.layer(layer.id + "__background", [&](LayerBuilder& builder) {
                        ImageParams params;
                        params.asset_path = layer.background->asset;
                        params.size = {static_cast<float>(plan.canvas.width),
                                       static_cast<float>(plan.canvas.height)};
                        params.fit = fit_mode(layer.background->fit.value_or(FitMode::Cover));
                        builder.image("background", std::move(params));
                        if (layer.background->opacity)
                            builder.opacity(*layer.background->opacity);
                        builder.from(layer.start_frame.value_or(Frame{0}));
                        builder.duration(layer.duration_frames.value_or(plan.canvas.duration));
                    });
                }
                for (std::size_t index = 0; index < plan.layers.size(); ++index) {
                    const auto& layer = plan.layers[index];
                    spdlog::info("[scene_build_layer] index={} id='{}' type={}", index, layer.id, static_cast<int>(layer.type));
                    scene.layer(layer.id, [&](LayerBuilder& builder) {
                        builder.font_engine(font_engine.get());
                        switch (layer.type) {
                            case LayerType::Image: {
                                ImageParams params;
                                params.asset_path = !layer.asset.empty() ? layer.asset : layer.source;
                                const bool preset_resolved = prepared_images[index].has_value();
                                params.size = {
                                    preset_resolved
                                        ? prepared_images[index]->box_width
                                        : layer.box_width.value_or(static_cast<float>(plan.canvas.width)),
                                    preset_resolved
                                        ? prepared_images[index]->box_height
                                        : layer.box_height.value_or(static_cast<float>(plan.canvas.height))};
                                params.fit = fit_mode(
                                    preset_resolved
                                        ? prepared_images[index]->fit
                                        : layer.fit.value_or(FitMode::Cover));
                                builder.image("image", std::move(params));
                                if (prepared_image_positions[index]) {
                                    builder.position({
                                        prepared_image_positions[index]->x,
                                        prepared_image_positions[index]->y,
                                        0.0f});
                                }
                                break;
                            }
                            case LayerType::Video: {
                                video::VideoSource source{.path = resolved_video_paths[index]};
                                if (layer.loop) source.loop_mode = video::VideoLoopMode::Loop;
                                builder.video(std::move(source));
                                break;
                            }
                            case LayerType::Color: {
                                RectParams params;
                                params.size = {static_cast<float>(plan.canvas.width),
                                               static_cast<float>(plan.canvas.height)};
                                params.color = {layer.color[0], layer.color[1],
                                                layer.color[2], layer.color[3]};
                                builder.rect("color", std::move(params));
                                break;
                            }
                            case LayerType::Text:
                                builder.kind(LayerKind::Text);
                                if (!prepared_texts[index])
                                    throw std::runtime_error("text layer was not prepared");
                                builder.text_run(
                                    "text", chronon3d::prepare_text(*prepared_texts[index]))
                                    .font_engine(font_engine.get());
                                break;
                            case LayerType::SubtitleTrack: {
                                if (!subtitles[index])
                                    throw std::runtime_error("subtitle asset was not prepared");
                                std::optional<ResolvedSubtitleStyle> subtitle_style;
                                if (layer.style)
                                    subtitle_style = resolve_subtitle_style(*layer.style);
                                float subtitle_font_size = 48.0f;
                                if (layer.font_size) {
                                    subtitle_font_size = *layer.font_size;
                                } else if (subtitle_style && subtitle_style->font_size) {
                                    subtitle_font_size = *subtitle_style->font_size;
                                }
                                authoring::Layer authoring_layer(builder, canvas);
                                authoring_layer.font_engine(font_engine.get());
                                auto track = authoring_layer.subtitles(*subtitles[index]);
                                track.preset(layer.preset.empty() ? "minimal_white" : layer.preset)
                                    .font(layer.font, subtitle_font_size)
                                    .box({layer.box_width.value_or(1800.0f),
                                          layer.box_height.value_or(200.0f)})
                                    .place(chronon3d::TextPlacementKind::SafeAreaBottom)
                                    .vertical_align(chronon3d::VerticalAlign::Bottom);
                                if (subtitle_style) {
                                    if (subtitle_style->fill) track.color(*subtitle_style->fill);
                                    if (subtitle_style->shadow) track.shadow(*subtitle_style->shadow);
                                }
                                track.build();
                                break;
                            }
                        }
                        apply_layer_timing(
                            builder, layer, plan.style_profile,
                            plan.canvas.duration,
                            layer.type == LayerType::Image &&
                                prepared_image_positions[index].has_value());
                    });
                }
                auto res_scene = scene.build();
                spdlog::info("[scene_built] layer_count={}", res_scene.layers().size());
                return res_scene;
            };
        definition.scene_content_fingerprint = content_fingerprint;

        const auto composition_t0 = chronon3d::profiling::now();
        auto compiled = chronon3d::compile_composition(definition, {});
        composition_compile_ms = chronon3d::profiling::duration_ms(
            composition_t0, chronon3d::profiling::now());
        if (!compiled) {
            return PlanDecodeError{"composition", compiled.error().message};
        }

        auto compiled_value = std::move(compiled).value();
        compiled_value.asset_manifest =
            std::make_shared<const chronon3d::assets::PreparedAssetManifest>(assets);
        compiled_value.render_budget = plan.budget;
        auto compiled_view = std::make_shared<const CompiledComposition>(compiled_value);
        auto composition = std::make_shared<Composition>(
            definition.composition,
            [compiled_view](const FrameContext& context) {
                auto evaluated = chronon3d::evaluate(
                    *compiled_view,
                    CompositionEvaluateContext{.frame_context = context},
                    context.local_time());
                if (!evaluated) {
                    throw std::runtime_error(
                        "compiled render-plan evaluation failed: " + evaluated.error().message);
                }
                const auto camera = evaluated.value().camera;
                Scene scene = std::move(evaluated.value().scene);
                if (camera.has_value()) scene.set_camera_2_5d(*camera);
                return scene;
            },
            content_fingerprint);
        PreparedRenderPlan prepared;
        prepared.compiled_composition = std::move(compiled_value);
        prepared.composition = std::shared_ptr<const Composition>(std::move(composition));
        prepared.assets = std::move(assets);
        prepared.resources = std::move(resources);
        prepared.fingerprint = fingerprints;
        prepared.job_id = plan.job_id;
        prepared.canvas = plan.canvas;
        prepared.output = plan.output;
        prepared.render_budget = plan.budget;
        spdlog::info(
            "[plan-compile-profile] assets={:.2f}ms fingerprint={:.2f}ms "
            "media_resolution={:.2f}ms materialize_layout={:.2f}ms "
            "composition_compile={:.2f}ms total={:.2f}ms",
            asset_prepare_ms, fingerprint_ms, media_resolution_ms,
            materialize_layout_ms, composition_compile_ms,
            chronon3d::profiling::duration_ms(
                compile_total_t0, chronon3d::profiling::now()));
        return prepared;
