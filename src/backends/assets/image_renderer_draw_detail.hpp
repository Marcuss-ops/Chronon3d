bool ImageRenderer::draw_image(const ImageShape& image, const RenderState& state, Framebuffer& fb) {
    if (image.path.empty() || image.size.x <= 0 || image.size.y <= 0) {
        return false;
    }

    std::shared_ptr<const CachedImage> cached_hold;
    const CachedImage* cached = nullptr;
    const auto t_decode0 = profiling::now();
    // Rendering is lookup-only: preparation owns all backend I/O and decode.
    cached_hold = m_cache.get().find(image.path, image.decode_options);
    cached = cached_hold.get();
    const auto t_decode1 = profiling::now();
    const double decode_ms = profiling::duration_ms(t_decode0, t_decode1);
    if (profiling::g_current_counters && decode_ms > 0.0) {
        profiling::g_current_counters->image_decode_wall_ms.fetch_add(
            static_cast<uint64_t>(std::llround(decode_ms)),
            std::memory_order_relaxed
        );
    }

    int img_w = 0;
    int img_h = 0;
    bool using_placeholder = false;

#ifdef CHRONON3D_USE_BLEND2D
    BLImage render_img;

    if (!cached || cached->bl_img.empty()) {
        int pw = static_cast<int>(std::round(image.size.x));
        int ph = static_cast<int>(std::round(image.size.y));
        pw = std::max(1, pw);
        ph = std::max(1, ph);

        render_img = BLImage(pw, ph, BL_FORMAT_PRGB32);
        BLContext ctx(render_img);
        ctx.setFillStyle(BLRgba32(0x3A, 0x0C, 0x0C, 0xFF)); // Deep red bg
        ctx.fillRect(0, 0, pw, ph);
        ctx.setStrokeStyle(BLRgba32(0xFF, 0x44, 0x44, 0xFF)); // Crimson border and cross
        ctx.setStrokeWidth(2.0f);
        ctx.strokeRect(0, 0, pw, ph);
        ctx.strokeLine(0, 0, pw, ph);
        ctx.strokeLine(0, ph, pw, 0);
        ctx.end();

        img_w = pw;
        img_h = ph;
        using_placeholder = true;
    }
    else {
        render_img = cached->bl_img;
        img_w = cached->width;
        img_h = cached->height;
    }
#else
    // Blend2D disabled: use framebuffer-only path
    if (!cached || !cached->fb_img) {
        // No valid cached image — can't render
        return false;
    }
    img_w = cached->width;
    img_h = cached->height;
    using_placeholder = false;
#endif

    const f32 final_opacity = image.opacity * state.opacity;

    // Handle ImageCrop (if enabled, crop origin and size are normalized relative to cached image)
    Vec2 original_source_size = Vec2(static_cast<float>(img_w), static_cast<float>(img_h));
    Vec2 effective_source_size = original_source_size;
    Vec2 effective_source_origin = Vec2(0.0f, 0.0f);
    
    // Skip crop on placeholders to avoid clipping fallback logic
    if (image.crop.enabled && !using_placeholder) {
        effective_source_size = image.crop.size * original_source_size;
        effective_source_origin = image.crop.origin * original_source_size;
    }

    // Call compute_media_placement
    // Force placeholders to use Stretch fit to match requested size exactly
    FitMode resolved_fit = using_placeholder ? FitMode::Stretch : image.fit;
    MediaPlacementResult placement = compute_media_placement(
        effective_source_size,
        image.size,
        resolved_fit,
        image.focal_point
    );

    Vec2 final_src_origin = effective_source_origin + placement.src_rect.origin;
    Vec2 final_src_size = placement.src_rect.size;

    int src_x = static_cast<int>(std::round(final_src_origin.x));
    int src_y = static_cast<int>(std::round(final_src_origin.y));
    int src_w = static_cast<int>(std::round(final_src_size.x));
    int src_h = static_cast<int>(std::round(final_src_size.y));

    // Clamp values to stay within bounds
    src_x = std::clamp(src_x, 0, img_w);
    src_y = std::clamp(src_y, 0, img_h);
    src_w = std::clamp(src_w, 0, img_w - src_x);
    src_h = std::clamp(src_h, 0, img_h - src_y);

    if (src_w <= 0 || src_h <= 0) return true;

    // Scale mapping from sub_img space [0,0 -> src_w,src_h] to destination dst_rect.
    // RenderGraph may provide a tight destination surface whose pixels are
    // local to `Framebuffer::origin_*`, while state.matrix remains in Canvas
    // coordinates. Convert that basis exactly once at this Canvas → surface
    // boundary; the sampler and compositor must only see surface-local
    // coordinates.
    Vec2 scale = placement.dst_rect.size / Vec2(static_cast<float>(src_w), static_cast<float>(src_h));
    const Mat4 canvas_to_surface = glm::translate(
        Mat4(1.0f),
        Vec3{-static_cast<float>(fb.origin_x()),
             -static_cast<float>(fb.origin_y()),
             0.0f});
    Mat4 scaled_model = canvas_to_surface * state.matrix
                      * glm::translate(Mat4(1.0f), Vec3(placement.dst_rect.origin, 0.0f))
                      * glm::scale(Mat4(1.0f), Vec3(scale, 1.0f));

    // Scale the radius from destination space to sub-image source space.
    float scaled_radius = 0.0f;
    if (image.radius > 0.0f && placement.dst_rect.size.x > 0.0f) {
        scaled_radius = image.radius * (static_cast<float>(src_w) / placement.dst_rect.size.x);
    }

    const bool mask_enabled = state.mask && state.mask->enabled();
    const int clip_x0 = state.clip_rect ? state.clip_rect->x0 : -1;
    const int clip_y0 = state.clip_rect ? state.clip_rect->y0 : -1;
    const int clip_x1 = state.clip_rect ? state.clip_rect->x1 : -1;
    const int clip_y1 = state.clip_rect ? state.clip_rect->y1 : -1;
    spdlog::debug(
        "[image-render] layer='{}' path='{}' cached={}x{} sub={}x{} requested={}x{} scale=({:.4f},{:.4f}) opacity={:.3f} mask={} clip={} clip_rect=[{},{} -> {},{}] tx={:.2f} ty={:.2f}",
        state.layer_id,
        image.path,
        img_w,
        img_h,
        src_w,
        src_h,
        image.size.x,
        image.size.y,
        scale.x,
        scale.y,
        final_opacity,
        mask_enabled ? 1 : 0,
        state.clip_rect ? 1 : 0,
        clip_x0,
        clip_y0,
        clip_x1,
        clip_y1,
        scaled_model[3][0],
        scaled_model[3][1]
    );

    const bool full_source = src_x == 0 && src_y == 0 && src_w == img_w && src_h == img_h;
    const bool use_cached_fb = full_source && !using_placeholder && image.fit == FitMode::Stretch && cached && cached->fb_img;

    const auto composite_start = profiling::now();

#ifdef CHRONON3D_USE_BLEND2D
    if (use_cached_fb && image.radius <= 0.0f) {
        blend2d_bridge::composite_framebuffer_transformed(fb, *cached->fb_img, scaled_model, final_opacity, BlendMode::Normal, &state);
    } else if (use_cached_fb && image.radius > 0.0f) {
        if (auto rounded = rounded_framebuffer(image.path, *cached, scaled_radius, image.decode_options)) {
            blend2d_bridge::composite_framebuffer_transformed(fb, *rounded, scaled_model, final_opacity, BlendMode::Normal, &state);
        }
    } else if (cached && cached->fb_img && !using_placeholder) {
        // Framebuffer fast-path: crop cached fb_img with memcpy (avoids Blend2D),
        // pre-apply radius, then composite via SIMD-accelerated path.
        Framebuffer cropped(src_w, src_h);
        cropped.set_opaque(false);

        // Parallel memcpy of cropped rows from cached framebuffer
        tbb::parallel_for(tbb::blocked_range<int>(0, src_h, 16), [&](const tbb::blocked_range<int>& range) {
            for (int y = range.begin(); y < range.end(); ++y) {
                std::memcpy(cropped.pixels_row(y),
                            cached->fb_img->pixels_row(src_y + y) + src_x,
                            static_cast<size_t>(src_w) * sizeof(Color));
            }
        });

        // Pre-apply rounded corners on the cropped framebuffer (parallelized)
        if (scaled_radius > 0.0f) {
            apply_rounded_coverage(cropped, scaled_radius);
        }

        blend2d_bridge::composite_framebuffer_transformed(fb, cropped, scaled_model, final_opacity, BlendMode::Normal, &state);
    } else {
        // Fallback: Blend2D path (for placeholders or uncached images)
        BLImage sub_img(src_w, src_h, BL_FORMAT_PRGB32);
        BLContext ctx(sub_img);
        ctx.blitImage(BLPointI(0, 0), render_img, BLRectI(src_x, src_y, src_w, src_h));
        ctx.end();
        blend2d_bridge::composite_bl_image_transformed(fb, sub_img, scaled_model, final_opacity, BlendMode::Normal, &state, scaled_radius);
    }
#else
    // Blend2D disabled: framebuffer-only compositing path
    if (use_cached_fb && image.radius <= 0.0f) {
        blend2d_bridge::composite_framebuffer_transformed(fb, *cached->fb_img, scaled_model, final_opacity, BlendMode::Normal, &state);
    } else if (use_cached_fb && image.radius > 0.0f) {
        if (auto rounded = rounded_framebuffer(image.path, *cached, scaled_radius, image.decode_options)) {
            blend2d_bridge::composite_framebuffer_transformed(fb, *rounded, scaled_model, final_opacity, BlendMode::Normal, &state);
        }
    } else if (cached && cached->fb_img && !using_placeholder) {
        Framebuffer cropped(src_w, src_h);
        cropped.set_opaque(false);
        tbb::parallel_for(tbb::blocked_range<int>(0, src_h, 16), [&](const tbb::blocked_range<int>& range) {
            for (int y = range.begin(); y < range.end(); ++y) {
                std::memcpy(cropped.pixels_row(y),
                            cached->fb_img->pixels_row(src_y + y) + src_x,
                            static_cast<size_t>(src_w) * sizeof(Color));
            }
        });
        if (scaled_radius > 0.0f) {
            apply_rounded_coverage(cropped, scaled_radius);
        }
        blend2d_bridge::composite_framebuffer_transformed(fb, cropped, scaled_model, final_opacity, BlendMode::Normal, &state);
    }
#endif

    const auto t_sample1 = profiling::now();
    const double sample_ms = profiling::duration_ms(composite_start, t_sample1);
    if (profiling::g_current_counters) {
        const double draw_us = profiling::duration_us(composite_start, t_sample1);
        profiling::g_current_counters->image_draw_wall_us.fetch_add(
            static_cast<uint64_t>(std::llround(draw_us)), std::memory_order_relaxed);
        profiling::g_current_counters->image_draw_count.fetch_add(1, std::memory_order_relaxed);
    }
    const uint64_t sampled_pixels = static_cast<uint64_t>(src_w) * static_cast<uint64_t>(src_h);
    record_image_telemetry(
        state,
        image.path,
        img_w,
        img_h,
        (cached && cached->valid() && !using_placeholder) ? "hit" : "miss_decode",
        decode_ms,
        sample_ms,
        sampled_pixels
    );

    return true;
}

bool ImageRenderer::draw_image_tiled(const ImageShape& image, const RenderState& state, Framebuffer& fb) {
    if (image.path.empty() || image.size.x <= 0 || image.size.y <= 0) {
        return false;
    }

    std::shared_ptr<const CachedImage> cached_hold;
    const CachedImage* cached = nullptr;
    const auto t_decode0 = profiling::now();
    // Rendering is lookup-only: preparation owns all backend I/O and decode.
    cached_hold = m_cache.get().find(image.path, image.decode_options);
    cached = cached_hold.get();
    const auto t_decode1 = profiling::now();
    const double decode_ms = profiling::duration_ms(t_decode0, t_decode1);
    if (profiling::g_current_counters && decode_ms > 0.0) {
        profiling::g_current_counters->image_decode_wall_ms.fetch_add(
            static_cast<uint64_t>(std::llround(decode_ms)),
            std::memory_order_relaxed
        );
    }

    if (!cached
#ifdef CHRONON3D_USE_BLEND2D
        || cached->bl_img.empty()
#endif
    ) {
        record_image_telemetry(
            state,
            image.path,
            cached ? cached->width : 0,
            cached ? cached->height : 0,
            "miss_decode",
            decode_ms,
            0.0,
            0
        );
        return false;
    }

    const f32 final_opacity = image.opacity * state.opacity;

    const float sx = image.size.x / static_cast<float>(cached->width);
    const float sy = image.size.y / static_cast<float>(cached->height);
    Mat4 scaled_model = state.matrix * glm::scale(Mat4(1.0f), Vec3(sx, sy, 1.0f));

    const bool mask_enabled = state.mask && state.mask->enabled();
    const int clip_x0 = state.clip_rect ? state.clip_rect->x0 : -1;
    const int clip_y0 = state.clip_rect ? state.clip_rect->y0 : -1;
    const int clip_x1 = state.clip_rect ? state.clip_rect->x1 : -1;
    const int clip_y1 = state.clip_rect ? state.clip_rect->y1 : -1;
    spdlog::debug(
        "[image-render-tiled] layer='{}' path='{}' cached={}x{} requested={}x{} scale=({:.4f},{:.4f}) opacity={:.3f} mask={} clip={} clip_rect=[{},{} -> {},{}] tx={:.2f} ty={:.2f}",
        state.layer_id,
        image.path,
        cached->width,
        cached->height,
        image.size.x,
        image.size.y,
        sx,
        sy,
        final_opacity,
        mask_enabled ? 1 : 0,
        state.clip_rect ? 1 : 0,
        clip_x0,
        clip_y0,
        clip_x1,
        clip_y1,
        scaled_model[3][0],
        scaled_model[3][1]
    );

    const auto composite_start = profiling::now();

#ifdef CHRONON3D_USE_BLEND2D
    blend2d_bridge::composite_bl_image_tiled(fb, cached->bl_img, scaled_model, final_opacity, BlendMode::Normal, &state);
#endif

    const auto t_sample1 = profiling::now();
    const double sample_ms = profiling::duration_ms(composite_start, t_sample1);
    if (profiling::g_current_counters) {
        const double draw_us = profiling::duration_us(composite_start, t_sample1);
        profiling::g_current_counters->image_draw_wall_us.fetch_add(
            static_cast<uint64_t>(std::llround(draw_us)), std::memory_order_relaxed);
        profiling::g_current_counters->image_draw_count.fetch_add(1, std::memory_order_relaxed);
    }
    record_image_telemetry(
        state,
        image.path,
        cached->width,
        cached->height,
        "hit",
        decode_ms,
        sample_ms,
        static_cast<uint64_t>(cached->width) * static_cast<uint64_t>(cached->height)
    );

    return true;
}

} // namespace chronon3d
