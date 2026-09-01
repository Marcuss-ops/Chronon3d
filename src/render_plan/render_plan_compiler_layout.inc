        const auto materialize_t0 = chronon3d::profiling::now();
        std::vector<std::optional<chronon3d::TextDefinition>> prepared_texts(plan.layers.size());
        for (std::size_t index = 0; index < plan.layers.size(); ++index) {
            const auto& layer = plan.layers[index];
            if (layer.type != LayerType::Text) continue;
            auto definition = materialize_text(layer, canvas);
            const auto resolved = resolver.resolve_logical(
                std::filesystem::path(definition.style.font.font_path));
            if (!resolved) {
                throw std::runtime_error(
                    "font asset was not prepared: " + definition.style.font.font_path);
            }
            definition.style.font.font_path = resolved->string();
            prepared_texts[index] = std::move(definition);
        }
        materialize_layout_ms = chronon3d::profiling::duration_ms(
            materialize_t0, chronon3d::profiling::now());
