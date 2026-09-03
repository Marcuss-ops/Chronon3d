graph::RenderOpResult draw_cached_text_run(
    RenderGraphContext& ctx,
    Framebuffer& destination,
    const TextRunShape& shape,
    const glm::mat4& model_matrix,
    float opacity) {
    if (!ctx.services.text_render_resources || !shape.layout ||
        shape.layout->placed.glyphs.empty() ||
        shape.glyphs.size() != shape.layout->placed.glyphs.size()) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::UnsupportedCapability,
            "draw_cached_text_run: CPU glyph atlas is not ready"});
    }

    // The surface text kernel consumes integer pixel quads. Fold a diagonal
    // affine scale into the glyph positions/scales so the common animated
    // subtitle/watermark scale path stays on GPU. Rotation, shear, reflection
    // and perspective still use the legacy renderer because GlyphInstance
    // cannot represent them without changing rasterization semantics.
    if (std::abs(model_matrix[0][1]) > 1e-4f ||
        std::abs(model_matrix[1][0]) > 1e-4f ||
        std::abs(model_matrix[0][2]) > 1e-4f ||
        std::abs(model_matrix[1][2]) > 1e-4f ||
        std::abs(model_matrix[2][0]) > 1e-4f ||
        std::abs(model_matrix[2][1]) > 1e-4f ||
        std::abs(model_matrix[2][3]) > 1e-4f ||
        model_matrix[0][0] <= 0.0f || model_matrix[1][1] <= 0.0f ||
        std::abs(model_matrix[3][3] - 1.0f) > 1e-4f) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::UnsupportedCapability,
            "draw_cached_text_run: non-translation transform"});
    }

    const auto& layout = *shape.layout;
    const int font_size = std::max(1, static_cast<int>(std::lround(layout.font_size)));
    const float model_scale_x = model_matrix[0][0];
    const float model_scale_y = model_matrix[1][1];
    const float model_translate_x = model_matrix[3][0];
    const float model_translate_y = model_matrix[3][1];
    const auto highlight_plan = build_timed_highlight_plan(shape);
    const float current_frame = static_cast<float>(ctx.frame_input.sample_time.frame);
    std::vector<GpuTextGlyph> glyphs;
    glyphs.reserve(layout.placed.glyphs.size());
    for (std::size_t i = 0; i < layout.placed.glyphs.size(); ++i) {
        const auto& state = shape.glyphs[i];
        // HarfBuzz uses glyph id 0 for whitespace/missing-glyph entries.
        // They have no bitmap atlas entry and must not invalidate an
        // otherwise fully resident run.
        if (state.glyph_id == 0) continue;
        const auto& placed = layout.placed.glyphs[i];
        // FreeType's vertical metrics are positive-up: bbox_y1 is normally
        // smaller than bbox_y0 in screen coordinates.  The bitmap dimensions
        // below, not the signed metric ordering, determine whether there is
        // drawable ink.
        if (placed.bbox_x1 <= placed.bbox_x0) continue;
        if (!(state.scale.x > 0.0f) || !(state.scale.y > 0.0f) ||
            std::abs(state.rotation.x) > 1e-4f ||
            std::abs(state.rotation.y) > 1e-4f ||
            std::abs(state.rotation.z) > 1e-4f ||
            std::abs(state.skew) > 1e-4f || state.blur > 1e-4f) {
            return graph::RenderOpResult(graph::RenderBackendError{
                graph::RenderBackendErrorCode::UnsupportedCapability,
                "draw_cached_text_run: animated/style feature requires canonical software text path"});
        }
        auto entry = ctx.services.text_render_resources->lookup_glyph_atlas(
            layout.font.font_path, state.glyph_id,
            static_cast<u32>(font_size));
        if ((!entry || !entry->image || entry->image->empty()) &&
            ctx.services.asset_resolver) {
            auto warmed = rasterize_missing_glyph(
                *ctx.services.text_render_resources, *ctx.services.asset_resolver,
                layout.font.font_path, state.glyph_id,
                static_cast<u32>(font_size), state.fill);
            if (warmed) {
                ctx.services.text_render_resources->store_glyph_atlas(
                    layout.font.font_path, state.glyph_id,
                    static_cast<u32>(font_size), *warmed);
                entry = std::move(warmed);
            }
        }
        if (!entry || !entry->image || entry->image->empty()) {
            return graph::RenderOpResult(graph::RenderBackendError{
                graph::RenderBackendErrorCode::UnsupportedCapability,
                "draw_cached_text_run: glyph bitmap not prewarmed"});
        }
        BLImageData data{};
        if (entry->image->getData(&data) != BL_SUCCESS ||
            data.format != BL_FORMAT_PRGB32 || data.size.w <= 0 || data.size.h <= 0) {
            return graph::RenderOpResult(graph::RenderBackendError{
                graph::RenderBackendErrorCode::UnsupportedCapability,
                "draw_cached_text_run: unsupported glyph bitmap format"});
        }
        const int stroke_radius = state.stroke.a > 1e-4f
            ? static_cast<int>(std::ceil(std::max(0.0f, state.stroke_width))) : 0;
        const int background_padding = state.background.a > 1e-4f ? 8 : 0;
        int shadow_padding = 0;
        for (const auto& shadow : shape.shadows) {
            if (!shadow.enabled || shadow.opacity <= 1e-4f) continue;
            shadow_padding = std::max(shadow_padding, static_cast<int>(std::ceil(
                std::max(std::abs(shadow.offset.x), std::abs(shadow.offset.y)) +
                std::max(0.0f, shadow.blur) + 1.0f)));
        }
        const int pad = std::max({stroke_radius, background_padding, shadow_padding});
        const int output_width = data.size.w + pad * 2;
        const int output_height = data.size.h + pad * 2;
        const Color raster_fill = highlight_plan.enabled ? shape.paint.fill : state.fill;
        std::string style_key;
        style_key.reserve(layout.font.font_path.size() + 128);
        style_key.append(layout.font.font_path);
        const auto append_key = [&](const auto& value) {
            style_key.append(reinterpret_cast<const char*>(&value), sizeof(value));
        };
        append_key(state.glyph_id);
        append_key(font_size);
        append_key(stroke_radius);
        append_key(background_padding);
        append_key(raster_fill);
        append_key(state.background);
        append_key(state.stroke);
        append_key(state.stroke_width);
        for (const auto& shadow : shape.shadows) {
            append_key(shadow.enabled);
            append_key(shadow.offset);
            append_key(shadow.blur);
            append_key(shadow.opacity);
            append_key(shadow.color);
        }
        auto styled = ctx.services.gpu_text_atlas_cache
            ? ctx.services.gpu_text_atlas_cache->find_styled(style_key)
            : std::shared_ptr<const runtime::GpuStyledGlyphCache::StyledGlyphBitmap>{};
        if (profiling::g_current_counters) {
            auto& counter = styled
                ? profiling::g_current_counters->gpu_text_styled_cache_hits
                : profiling::g_current_counters->gpu_text_styled_cache_misses;
            counter.fetch_add(1, std::memory_order_relaxed);
        }
        if (!styled) {
            // SOLO generazione bitmap/cache miss
            if (output_width <= 0 || output_height <= 0) {
                return graph::RenderOpResult(graph::RenderBackendError{
                    graph::RenderBackendErrorCode::InvalidInput,
                    "draw_cached_text_run: invalid styled glyph raster dimensions"});
            }
            std::size_t raster_pixels = 0;
            std::size_t raster_floats = 0;
            if (!checked_mul(static_cast<std::size_t>(output_width),
                             static_cast<std::size_t>(output_height), raster_pixels) ||
                !checked_mul(raster_pixels, 4u, raster_floats)) {
                return graph::RenderOpResult(graph::RenderBackendError{
                    graph::RenderBackendErrorCode::InvalidInput,
                    "draw_cached_text_run: styled glyph raster size overflow"});
            }
            std::vector<float> rgba(raster_floats, 0.0f);
            auto source_alpha = [&](int x, int y) {
                if (x < 0 || y < 0 || x >= data.size.w || y >= data.size.h) return 0.0f;
                const auto* source_row = static_cast<const std::uint8_t*>(data.pixelData) +
                    static_cast<std::size_t>(y) * data.stride;
                return source_row[static_cast<std::size_t>(x) * 4 + 3] / 255.0f;
            };
            auto over = [](float& dr, float& dg, float& db, float& da,
                           float sr, float sg, float sb, float sa) {
                dr = sr + dr * (1.0f - sa);
                dg = sg + dg * (1.0f - sa);
                db = sb + db * (1.0f - sa);
                da = sa + da * (1.0f - sa);
            };
            auto shadow_alpha = [&](const TextShadow& shadow, int x, int y) {
                const int sx = x - static_cast<int>(std::lround(shadow.offset.x));
                const int sy = y - static_cast<int>(std::lround(shadow.offset.y));
                const int radius = static_cast<int>(std::ceil(std::max(0.0f, shadow.blur)));
                if (radius <= 0) return source_alpha(sx, sy);
                float sum = 0.0f;
                int samples = 0;
                for (int oy = -1; oy <= 1; ++oy) {
                    for (int ox = -1; ox <= 1; ++ox) {
                        sum += source_alpha(sx + ox * radius, sy + oy * radius);
                        ++samples;
                    }
                }
                return sum / static_cast<float>(samples);
            };
            for (int y = 0; y < output_height; ++y) {
                const int source_y = y - pad;
                for (int x = 0; x < output_width; ++x) {
                    const int source_x = x - pad;
                    const float mask = source_alpha(source_x, source_y);
                    float stroke_mask = 0.0f;
                    if (stroke_radius > 0) {
                        for (int oy = -stroke_radius; oy <= stroke_radius; ++oy) {
                            for (int ox = -stroke_radius; ox <= stroke_radius; ++ox) {
                                if (ox * ox + oy * oy <= stroke_radius * stroke_radius) {
                                    stroke_mask = std::max(stroke_mask,
                                        source_alpha(source_x + ox, source_y + oy));
                                }
                            }
                        }
                    }
                    const std::size_t off = (static_cast<std::size_t>(y) * output_width + x) * 4;
                    float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
                    if (state.background.a > 1e-4f) {
                        const float ba = std::clamp(state.background.a, 0.0f, 1.0f);
                        over(r, g, b, a, state.background.r * ba,
                             state.background.g * ba, state.background.b * ba, ba);
                    }
                    for (const auto& shadow : shape.shadows) {
                        if (!shadow.enabled || shadow.opacity <= 1e-4f) continue;
                        const float sa = shadow_alpha(shadow, source_x, source_y) *
                            std::clamp(shadow.opacity, 0.0f, 1.0f);
                        over(r, g, b, a, shadow.color.r * sa,
                             shadow.color.g * sa, shadow.color.b * sa, sa);
                    }
                    if (stroke_mask > 1e-4f && state.stroke.a > 1e-4f) {
                        const float sa = stroke_mask * std::clamp(state.stroke.a, 0.0f, 1.0f);
                        over(r, g, b, a, state.stroke.r * sa,
                             state.stroke.g * sa, state.stroke.b * sa, sa);
                    }
                    if (mask > 1e-4f) {
                        const float fa = mask * std::clamp(raster_fill.a, 0.0f, 1.0f);
                        over(r, g, b, a, raster_fill.r * fa,
                             raster_fill.g * fa, raster_fill.b * fa, fa);
                    }
                    rgba[off + 0] = r;
                    rgba[off + 1] = g;
                    rgba[off + 2] = b;
                    rgba[off + 3] = a;
                }
            }
            auto pixels = std::make_shared<const std::vector<float>>(std::move(rgba));
            if (ctx.services.gpu_text_atlas_cache) {
                ctx.services.gpu_text_atlas_cache->store_styled(
                    style_key, static_cast<std::uint32_t>(output_width),
                    static_cast<std::uint32_t>(output_height), pixels);
            }
            styled = std::make_shared<const runtime::GpuStyledGlyphCache::StyledGlyphBitmap>(
                runtime::GpuStyledGlyphCache::StyledGlyphBitmap{
                    static_cast<std::uint32_t>(output_width),
                    static_cast<std::uint32_t>(output_height), std::move(pixels)});
        }

        // ===========================================
        // DA QUI IN POI FUORI DA if (!styled)
        // ===========================================

        if (!styled || !styled->rgba) {
            return graph::RenderOpResult(graph::RenderBackendError{
                graph::RenderBackendErrorCode::ExecutionFailure,
                "draw_cached_text_run: styled glyph cache returned no pixels"});
        }

        std::size_t glyph_pixels = 0;
        std::size_t expected_pixels = 0;
        if (!checked_mul(static_cast<std::size_t>(styled->width),
                         static_cast<std::size_t>(styled->height), glyph_pixels) ||
            !checked_mul(glyph_pixels, 4u, expected_pixels) ||
            styled->rgba->size() != expected_pixels) {
            return graph::RenderOpResult(graph::RenderBackendError{
                graph::RenderBackendErrorCode::InvalidInput,
                "draw_cached_text_run: cached glyph pixel-size mismatch"});
        }

        const float highlight_start =
            highlight_plan.enabled &&
            i < highlight_plan.glyph_ranges.size()
                ? highlight_plan.glyph_ranges[i].first
                : -1.0f;

        const float highlight_end =
            highlight_plan.enabled &&
            i < highlight_plan.glyph_ranges.size()
                ? highlight_plan.glyph_ranges[i].second
                : -1.0f;

        glyphs.push_back(GpuTextGlyph{
            styled->width,
            styled->height,
            {},
            static_cast<std::int32_t>(
                std::lround(
                    model_translate_x +
                    model_scale_x *
                        (placed.x +
                         state.position.x +
                         entry->x_offset -
                         pad))),
            static_cast<std::int32_t>(
                std::lround(
                    model_translate_y +
                    model_scale_y *
                        (placed.y +
                         state.position.y +
                         entry->y_offset -
                         pad))),
            state.scale.x * model_scale_x,
            state.scale.y * model_scale_y,
            state.opacity * opacity,
            styled->rgba,
            highlight_start,
            highlight_end
        });

        glyphs.back().atlas_key = style_key;
        if (profiling::g_current_counters) {
            profiling::g_current_counters->gpu_text_glyphs_built.fetch_add(1, std::memory_order_relaxed);
        }
    }
    // A cached TextRun renders into its own transparent output surface. Do
    // not upload the CPU framebuffer here: that would copy the full canvas
    // once per text layer and frame even though the text kernel writes only
    // the glyph result.
    if (!ensure_empty_native_surface(ctx, destination)) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::UnsupportedCapability,
            "draw_cached_text_run: destination has no native surface"});
    }
    const auto cleared = ctx.services.backend->fill_rect_surface(
        destination.surface_handle(), 0, 0, destination.width(), destination.height(), Color{});
    if (!cleared.ok()) return cleared;

    auto result = draw_packed_text_run_surface(
        ctx, destination.surface_handle(), glyphs, current_frame,
        highlight_plan.color, highlight_plan.enabled);
    return result;
}
