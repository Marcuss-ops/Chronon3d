        const auto materialize_t0 = chronon3d::profiling::now();
        std::vector<std::optional<chronon3d::TextDefinition>> prepared_texts(plan.layers.size());
        std::vector<std::optional<chronon3d::Vec2>> prepared_image_positions(plan.layers.size());
        std::vector<std::optional<ResolvedImageLayer>> prepared_images(plan.layers.size());
        struct LayoutTarget {
            std::size_t layer_index;
            bool is_image;
        };
        std::vector<chronon3d::layout::OverlayLayoutRequest> layout_requests;
        std::vector<LayoutTarget> layout_targets;
        for (std::size_t index = 0; index < plan.layers.size(); ++index) {
            const auto& layer = plan.layers[index];
            if (layer.type == LayerType::Text) {
                auto materialized = materialize_text(
                    layer, canvas, plan.style_profile,
                    plan.canvas.duration.value, *font_engine);
                if (layer.position_dimensions > 0) {
                    materialized.definition.frame.anchor = chronon3d::TextAnchor::Center;
                    materialized.definition.frame.align = chronon3d::TextAlign::Center;
                    materialized.definition.frame.placement = chronon3d::TextPlacement{
                        chronon3d::TextPlacementKind::Absolute,
                        {0.0f, 0.0f}};
                } else if (materialized.layout) {
                    layout_requests.push_back(layout_request(
                        layer, *materialized.layout, plan.canvas.duration.value));
                    layout_targets.push_back({index, false});
                }
                prepared_texts[index] = std::move(materialized.definition);
            } else if (layer.type == LayerType::Image) {
                ResolvedImageLayer resolved;
                resolved.box_width = layer.box_width.value_or(320.0f);
                resolved.box_height = layer.box_height.value_or(320.0f);
                ResolvedLayoutIntent image_layout;
                image_layout.intent = layer.anchor ? layer.anchor->type : "center";
                image_layout.width = resolved.box_width;
                image_layout.height = resolved.box_height;
                image_layout.safe_margin = layer.anchor ? layer.anchor->safe_margin : 0.06f;
                image_layout.fallback_intents = {};
                resolved.layout.intent = image_layout.intent;
                resolved.layout.width = image_layout.width;
                resolved.layout.height = image_layout.height;
                resolved.layout.safe_margin = image_layout.safe_margin;
                if (layer.position_dimensions > 0) {
                    prepared_images[index] = resolved;
                    const float box_width = resolved.box_width;
                    const float box_height = resolved.box_height;
                    const float world_x = layer.position[0] + box_width * 0.5f -
                                          static_cast<float>(plan.canvas.width) * 0.5f;
                    const float world_y = layer.position[1] + box_height * 0.5f -
                                          static_cast<float>(plan.canvas.height) * 0.5f;
                    prepared_image_positions[index] =
                        chronon3d::Vec2{world_x + layer.offset[0], world_y + layer.offset[1]};
                } else {
                    layout_requests.push_back(layout_request(
                        layer, resolved.layout, plan.canvas.duration.value));
                    layout_targets.push_back({index, true});
                    prepared_images[index] = resolved;
                }
            }
        }

        for (auto& prepared : prepared_texts) {
            if (!prepared || prepared->style.font.font_path.empty()) continue;
            const auto resolved = resolver.resolve_logical(
                std::filesystem::path{prepared->style.font.font_path});
            if (!resolved) {
                throw std::runtime_error(
                    "font asset was not prepared: " + prepared->style.font.font_path);
            }
            prepared->style.font.font_path = resolved->string();
        }

        const auto placements = chronon3d::layout::OverlayLayoutResolver{}.solve(
            canvas.width, canvas.height, std::move(layout_requests), {});

        for (std::size_t i = 0; i < placements.size(); ++i) {
            const auto& placement = placements[i];
            if (!placement.valid) {
                throw std::runtime_error(placement.warning);
            }
            const auto& target = layout_targets[i];
            if (target.is_image) {
                const auto& image_layer = plan.layers[target.layer_index];
                const auto& resolved_image = *prepared_images[target.layer_index];
                const float box_width = resolved_image.box_width;
                const float box_height = resolved_image.box_height;
                const float world_x = placement.x + box_width * 0.5f -
                                      static_cast<float>(plan.canvas.width) * 0.5f;
                const float world_y = placement.y + box_height * 0.5f -
                                      static_cast<float>(plan.canvas.height) * 0.5f;
                prepared_image_positions[target.layer_index] =
                    chronon3d::Vec2{world_x + image_layer.offset[0],
                                    world_y + image_layer.offset[1]};
            } else {
                auto& definition = *prepared_texts[target.layer_index];
                definition.frame.anchor = chronon3d::TextAnchor::TopLeft;
                definition.frame.align = chronon3d::TextAlign::Left;
                definition.frame.placement = chronon3d::TextPlacement{
                    chronon3d::TextPlacementKind::Absolute,
                    {placement.x + plan.layers[target.layer_index].offset[0],
                     placement.y + plan.layers[target.layer_index].offset[1]}};
            }
        }
        materialize_layout_ms = chronon3d::profiling::duration_ms(
            materialize_t0, chronon3d::profiling::now());
