// Vulkan Impl surface operation implementations. Included inside VulkanBackend::Impl to keep
// the private state definition single-source while separating responsibilities.
    void composite(runtime::RenderSurfaceHandle destination,
                   runtime::RenderSurfaceHandle source, BlendMode mode,
                   const std::optional<raster::BBox>& clip = std::nullopt,
                   bool replace = false) {
        ++stats.legacy_composite_calls;
        auto& dst_image = resolve_image(destination);
        auto& src_image = resolve_image(source);
        if (!src_image.initialized ||
            dst_image.width != src_image.width ||
            dst_image.height != src_image.height) {
            throw std::invalid_argument(
                "Vulkan composite references incompatible surfaces: dst=" +
                std::to_string(dst_image.width) + "x" + std::to_string(dst_image.height) +
                " src=" + std::to_string(src_image.width) + "x" +
                std::to_string(src_image.height) +
                (src_image.initialized ? "" : " src-uninitialized"));
        }
        const std::int32_t blend_mode = replace ? 2 : (mode == BlendMode::Add ? 1 : 0);
        if (frame_batch.active) {
            const auto descriptors = allocate_pass_descriptor_set();
            write_descriptors(descriptors, dst_image, src_image);
            const auto cmd = active_command_buffer();
            emit_pass_sync(cmd, {&dst_image, &src_image});
            record_composite(cmd, descriptors, dst_image, src_image,
                             blend_mode, 1.0f, kIdentityTint, clip);
            dst_image.initialized = true;
            ++frame_batch.pass_count;
            ++stats.passes_executed;
            return;
        }
        bind_descriptors(dst_image, src_image);
        begin_command_buffer();
        emit_conservative_pass_sync(command_buffer, {&dst_image, &src_image});
        record_composite(command_buffer, descriptor_set, dst_image, src_image,
                         blend_mode, 1.0f, kIdentityTint, clip);
        dst_image.initialized = true;
        submit();
    }

    void fill_rect(runtime::RenderSurfaceHandle destination,
                   std::int32_t x0, std::int32_t y0,
                   std::int32_t x1, std::int32_t y1,
                   const Color& color) {
        auto& dst_image = resolve_image(destination);
        if (dst_image.width == 0 || dst_image.height == 0) {
            throw std::invalid_argument("Vulkan fill_rect references an empty surface");
        }
        if (frame_batch.active) {
            const auto descriptors = allocate_pass_descriptor_set();
            write_fill_rect_descriptors(descriptors, dst_image);
            const auto cmd = active_command_buffer();
            emit_pass_sync(cmd, {&dst_image});
            record_fill_rect(cmd, descriptors, dst_image, x0, y0, x1, y1, color);
            dst_image.initialized = true;
            ++frame_batch.pass_count;
            ++stats.passes_executed;
            return;
        }
        bind_fill_rect_descriptors(dst_image);
        begin_command_buffer();
        emit_conservative_pass_sync(command_buffer, {&dst_image});
        record_fill_rect(command_buffer, descriptor_set, dst_image, x0, y0, x1, y1, color);
        dst_image.initialized = true;
        submit();
    }

    void transform(runtime::RenderSurfaceHandle destination,
                   runtime::RenderSurfaceHandle source,
                   int offset_x, int offset_y, float opacity) {
        ++stats.legacy_transform_calls;
        auto& dst_image = resolve_image(destination);
        auto& src_image = resolve_image(source);
        if (!src_image.initialized ||
            dst_image.width == 0 || dst_image.height == 0) {
            throw std::invalid_argument("Vulkan transform references incompatible surfaces");
        }
        if (frame_batch.active) {
            const auto descriptors = allocate_pass_descriptor_set();
            write_descriptors(descriptors, dst_image, src_image);
            const auto cmd = active_command_buffer();
            emit_pass_sync(cmd, {&dst_image, &src_image});
            record_transform(cmd, descriptors,
                             dst_image, src_image, offset_x, offset_y, opacity);
            dst_image.initialized = true;
            ++frame_batch.pass_count;
            ++stats.passes_executed;
            return;
        }
        bind_descriptors(dst_image, src_image);
        begin_command_buffer();
        emit_conservative_pass_sync(command_buffer, {&dst_image, &src_image});
        record_transform(command_buffer, descriptor_set, dst_image, src_image,
                         offset_x, offset_y, opacity);
        dst_image.initialized = true;
        submit();
    }

    void transform_affine(runtime::RenderSurfaceHandle destination,
                          runtime::RenderSurfaceHandle source,
                          const runtime::SurfaceAffineTransform& transform) {
        ++stats.legacy_transform_calls;
        auto& dst_image = resolve_image(destination);
        auto& src_image = resolve_image(source);
        if (!src_image.initialized || dst_image.width == 0 || dst_image.height == 0) {
            throw std::invalid_argument("Vulkan affine transform references incompatible surfaces");
        }
        if (frame_batch.active) {
            const auto descriptors = allocate_pass_descriptor_set();
            write_descriptors(descriptors, dst_image, src_image);
            const auto cmd = active_command_buffer();
            emit_pass_sync(cmd, {&dst_image, &src_image});
            record_transform_affine(cmd, descriptors,
                                    dst_image, src_image, transform);
            dst_image.initialized = true;
            ++frame_batch.pass_count;
            ++stats.passes_executed;
            return;
        }
        bind_descriptors(dst_image, src_image);
        begin_command_buffer();
        emit_conservative_pass_sync(command_buffer, {&dst_image, &src_image});
        record_transform_affine(command_buffer, descriptor_set,
                                dst_image, src_image, transform);
        dst_image.initialized = true;
        submit();
    }

    void blur(runtime::RenderSurfaceHandle destination,
              runtime::RenderSurfaceHandle source, float radius, bool horizontal) {
        if (!(radius >= 0.0f) || radius > 32.0f) {
            throw std::invalid_argument("Vulkan blur radius must be within [0, 32]");
        }
        auto& dst_image = resolve_image(destination);
        auto& src_image = resolve_image(source);
        if (!src_image.initialized ||
            dst_image.width != src_image.width ||
            dst_image.height != src_image.height) {
            throw std::invalid_argument("Vulkan blur references incompatible surfaces");
        }
        if (frame_batch.active) {
            const auto descriptors = allocate_pass_descriptor_set();
            write_descriptors(descriptors, dst_image, src_image);
            const auto cmd = active_command_buffer();
            emit_pass_sync(cmd, {&dst_image, &src_image});
            record_blur(cmd, descriptors, dst_image, src_image, radius, horizontal);
            dst_image.initialized = true;
            ++frame_batch.pass_count;
            ++stats.passes_executed;
            return;
        }
        bind_descriptors(dst_image, src_image);
        begin_command_buffer();
        emit_conservative_pass_sync(command_buffer, {&dst_image, &src_image});
        record_blur(command_buffer, descriptor_set,
                    dst_image, src_image, radius, horizontal);
        dst_image.initialized = true;
        submit();
    }

    void glow(runtime::RenderSurfaceHandle destination,
              runtime::RenderSurfaceHandle source,
              runtime::RenderSurfaceHandle scratch_horizontal,
              runtime::RenderSurfaceHandle scratch_vertical,
              float radius, float intensity, const Color& tint) {
        if (!(radius >= 0.0f) || radius > 32.0f) {
            throw std::invalid_argument("Vulkan glow radius must be within [0, 32]");
        }
        auto find_surface = [&](runtime::RenderSurfaceHandle handle) -> Image& {
            return resolve_image(handle);
        };
        auto& dst = find_surface(destination);
        auto& src = find_surface(source);
        auto& horizontal = find_surface(scratch_horizontal);
        auto& vertical = find_surface(scratch_vertical);
        if (!src.initialized || !dst.initialized ||
            src.width != dst.width || src.height != dst.height ||
            horizontal.width != src.width || horizontal.height != src.height ||
            vertical.width != src.width || vertical.height != src.height) {
            throw std::invalid_argument("Vulkan glow surfaces have incompatible dimensions");
        }
        const float tint_rgba[4] = {tint.r, tint.g, tint.b, tint.a};

        if (frame_batch.active) {
            const auto horizontal_descriptor = allocate_pass_descriptor_set();
            write_descriptors(horizontal_descriptor, horizontal, src);
            const auto vertical_descriptor = allocate_pass_descriptor_set();
            write_descriptors(vertical_descriptor, vertical, horizontal);
            const auto composite_descriptor = allocate_pass_descriptor_set();
            write_descriptors(composite_descriptor, dst, vertical);
            // Glow is a single plan pass whose internal blur→blur→composite
            // chain is a write→read dependency chain between dispatches; the
            // full barriers below are glow's own structure (the only
            // multi-dispatch operation), so the pass-level mapper call is
            // skipped for it.
            const auto cmd = active_command_buffer();
            emit_conservative_pass_sync(cmd, {&horizontal, &src});
            record_blur(cmd, horizontal_descriptor, horizontal, src, radius, true);
            emit_conservative_pass_sync(cmd, {&vertical, &horizontal});
            record_blur(cmd, vertical_descriptor, vertical, horizontal, radius, false);
            emit_conservative_pass_sync(cmd, {&dst, &vertical});
            record_composite(cmd, composite_descriptor, dst, vertical,
                             1, intensity, tint_rgba);
            horizontal.initialized = true;
            vertical.initialized = true;
            ++frame_batch.pass_count;
            ++stats.passes_executed;
            return;
        }

        begin_command_buffer();
        const auto horizontal_descriptor = ensure_glow_descriptor_set(0);
        write_descriptors(horizontal_descriptor, horizontal, src);
        const auto vertical_descriptor = ensure_glow_descriptor_set(1);
        write_descriptors(vertical_descriptor, vertical, horizontal);
        const auto composite_descriptor = ensure_glow_descriptor_set(2);
        write_descriptors(composite_descriptor, dst, vertical);
        emit_conservative_pass_sync(command_buffer, {&horizontal, &src});
        record_blur(command_buffer, horizontal_descriptor,
                    horizontal, src, radius, true);
        emit_conservative_pass_sync(command_buffer, {&vertical, &horizontal});
        record_blur(command_buffer, vertical_descriptor,
                    vertical, horizontal, radius, false);
        emit_conservative_pass_sync(command_buffer, {&dst, &vertical});
        record_composite(command_buffer, composite_descriptor,
                         dst, vertical, 1, intensity, tint_rgba);
        horizontal.initialized = true;
        vertical.initialized = true;
        submit();
    }

    void color_adjust(runtime::RenderSurfaceHandle destination,
                      runtime::RenderSurfaceHandle source,
                      float brightness, float contrast,
                      const Color& tint, float tint_amount) {
        auto& dst = resolve_image(destination);
        auto& src = resolve_image(source);
        if (!src.initialized ||
            dst.width != src.width ||
            dst.height != src.height) {
            throw std::invalid_argument("Vulkan color adjust references incompatible surfaces");
        }
        if (frame_batch.active) {
            const auto descriptors = allocate_pass_descriptor_set();
            write_descriptors(descriptors, dst, src);
            const auto cmd = active_command_buffer();
            emit_pass_sync(cmd, {&dst, &src});
            record_color_adjust(cmd, descriptors,
                                dst, src, brightness, contrast, tint, tint_amount);
            dst.initialized = true;
            ++frame_batch.pass_count;
            ++stats.passes_executed;
            return;
        }
        bind_descriptors(dst, src);
        begin_command_buffer();
        emit_conservative_pass_sync(command_buffer, {&dst, &src});
        record_color_adjust(command_buffer, descriptor_set,
                            dst, src, brightness, contrast, tint, tint_amount);
        dst.initialized = true;
        submit();
    }

    void matte(runtime::RenderSurfaceHandle destination,
               runtime::RenderSurfaceHandle target,
               runtime::RenderSurfaceHandle matte_surface,
               bool luma, bool inverted) {
        auto& dst = resolve_image(destination);
        auto& target_image = resolve_image(target);
        auto& matte_image = resolve_image(matte_surface);
        if (!target_image.initialized || !matte_image.initialized ||
            dst.width != target_image.width ||
            dst.height != target_image.height ||
            dst.width != matte_image.width ||
            dst.height != matte_image.height) {
            throw std::invalid_argument("Vulkan matte references incompatible surfaces");
        }
        if (frame_batch.active) {
            const auto descriptors = allocate_pass_descriptor_set();
            write_matte_descriptors(descriptors, dst, target_image, matte_image);
            const auto cmd = active_command_buffer();
            emit_pass_sync(cmd, {&dst, &target_image, &matte_image});
            record_matte(cmd, descriptors,
                         dst, target_image, matte_image, luma, inverted);
            dst.initialized = true;
            ++frame_batch.pass_count;
            ++stats.passes_executed;
            return;
        }
        ensure_descriptor_set();
        write_matte_descriptors(descriptor_set, dst, target_image, matte_image);
        begin_command_buffer();
        emit_conservative_pass_sync(command_buffer, {&dst, &target_image, &matte_image});
        record_matte(command_buffer, descriptor_set,
                     dst, target_image, matte_image, luma, inverted);
        dst.initialized = true;
        submit();
    }

    // GPU text-run primitive: lowered directly to canonical draw_text_batch.
    void text_run_surface(runtime::RenderSurfaceHandle destination,
                          runtime::RenderSurfaceHandle atlas,
                          std::span<const runtime::GlyphInstance> glyphs,
                          float current_frame = 0.0f,
                          const Color& highlight_color = Color{},
                          bool highlight_enabled = false) {
        (void)current_frame;
        (void)highlight_color;
        (void)highlight_enabled;
        if (glyphs.empty()) {
            throw std::invalid_argument("Vulkan text run requires at least one glyph");
        }
        ++stats.legacy_text_run_surface_calls;

        std::vector<runtime::GlyphStatic> static_glyphs;
        static_glyphs.reserve(glyphs.size());
        std::vector<runtime::TextRunDynamic> runs;
        runs.reserve(glyphs.size());

        for (std::size_t i = 0; i < glyphs.size(); ++i) {
            const auto& g = glyphs[i];
            runtime::TextRunDynamic run;
            run.tx = static_cast<float>(g.dst_x);
            run.ty = static_cast<float>(g.dst_y);
            run.sx = (g.scale_x != 0.0f) ? g.scale_x : 1.0f;
            run.sy = (g.scale_y != 0.0f) ? g.scale_y : 1.0f;
            run.opacity = g.opacity;
            run.color = 0xFFFFFFFF;
            runs.push_back(run);

            runtime::GlyphStatic gs;
            gs.run_index = static_cast<std::uint32_t>(i);
            gs.atlas_page = 0;
            gs.flags = 0;
            gs.atlas_x = static_cast<std::uint16_t>(g.atlas_x >= 0 ? g.atlas_x : 0);
            gs.atlas_y = static_cast<std::uint16_t>(g.atlas_y >= 0 ? g.atlas_y : 0);
            gs.atlas_w = static_cast<std::uint16_t>(g.width > 0 ? g.width : 0);
            gs.atlas_h = static_cast<std::uint16_t>(g.height > 0 ? g.height : 0);
            gs.plane_left = 0.0f;
            gs.plane_top = 0.0f;
            gs.plane_right = static_cast<float>(g.width);
            gs.plane_bottom = static_cast<float>(g.height);
            gs.draw_order = static_cast<std::uint32_t>(i);
            static_glyphs.push_back(gs);
        }

        std::array<runtime::RenderSurfaceHandle, 1> pages = {atlas};
        draw_text_batch(destination, static_glyphs, runs, pages);
    }

    // ── draw_text_batch ──────────────────────────────────────────────────
    // Draw a text run directly into `destination` — zero intermediate
    // text framebuffers, zero clear overhead.
    //
    // `glyphs` holds immutable per-glyph static geometry (atlas coordinates,
    // plane bounds, draw order); `runs` holds per-run dynamic state (transforms,
    // opacities, tints); `atlas_pages` maps per-glyph atlas_page
    // indices to VkImages.
    struct alignas(16) GpuGlyphStatic {
        std::uint32_t run_index{0};
        std::uint32_t atlas_page_and_flags{0};
        std::uint32_t atlas_pos{0};
        std::uint32_t atlas_size{0};
        float plane_left{0.0f};
        float plane_top{0.0f};
        float plane_right{0.0f};
        float plane_bottom{0.0f};
        std::uint32_t draw_order{0};
        std::uint32_t pad0{0};
        std::uint32_t pad1{0};
        std::uint32_t pad2{0};
    };
    static_assert(sizeof(GpuGlyphStatic) == 48, "GpuGlyphStatic must be 48 bytes matching std430");

    struct alignas(16) GpuTextRunDynamic {
        float tx{0.0f};
        float ty{0.0f};
        float sx{1.0f};
        float sy{1.0f};
        float opacity{1.0f};
        std::uint32_t color{0xFFFFFFFF};
        float pad0{0.0f};
        float pad1{0.0f};
    };
    static_assert(sizeof(GpuTextRunDynamic) == 32, "GpuTextRunDynamic must be 32 bytes");

    void draw_text_batch(runtime::RenderSurfaceHandle destination,
                         std::span<const runtime::GlyphStatic> glyphs,
                         std::span<const runtime::TextRunDynamic> runs,
                         std::span<const runtime::RenderSurfaceHandle> atlas_pages) {
        if (glyphs.empty() || runs.empty() || atlas_pages.empty()) return;
        ++stats.text_batch_calls;
        stats.glyphs_processed += glyphs.size();

        auto& dst_image = resolve_image(destination);
        if (!dst_image.initialized || dst_image.width == 0 || dst_image.height == 0) {
            throw std::invalid_argument(
                "Vulkan draw_text_batch references an invalid destination surface");
        }
        auto& atlas_image = resolve_image(atlas_pages[0]);
        if (!atlas_image.initialized || atlas_image.width == 0 || atlas_image.height == 0) {
            throw std::invalid_argument(
                "Vulkan draw_text_batch references an invalid atlas surface");
        }

        std::array<GpuGlyphStatic, 1024> local_glyphs;
        std::vector<GpuGlyphStatic> heap_glyphs;
        GpuGlyphStatic* gpu_glyphs = local_glyphs.data();
        if (glyphs.size() > local_glyphs.size()) {
            heap_glyphs.resize(glyphs.size());
            gpu_glyphs = heap_glyphs.data();
        }

        std::array<GpuTextRunDynamic, 128> local_runs;
        std::vector<GpuTextRunDynamic> heap_runs;
        GpuTextRunDynamic* gpu_runs = local_runs.data();
        if (runs.size() > local_runs.size()) {
            heap_runs.resize(runs.size());
            gpu_runs = heap_runs.data();
        }

        for (std::size_t r = 0; r < runs.size(); ++r) {
            const auto& run = runs[r];
            auto& gr = gpu_runs[r];
            gr.tx = run.tx;
            gr.ty = run.ty;
            gr.sx = (run.sx != 0.0f) ? run.sx : 1.0f;
            gr.sy = (run.sy != 0.0f) ? run.sy : 1.0f;
            gr.opacity = run.opacity;
            gr.color = run.color;
            gr.pad0 = 0.0f;
            gr.pad1 = 0.0f;
        }

        std::int32_t dispatch_x0 = static_cast<std::int32_t>(dst_image.width);
        std::int32_t dispatch_y0 = static_cast<std::int32_t>(dst_image.height);
        std::int32_t dispatch_x1 = 0;
        std::int32_t dispatch_y1 = 0;

        for (std::size_t i = 0; i < glyphs.size(); ++i) {
            const auto& gs = glyphs[i];
            auto& gg = gpu_glyphs[i];
            gg.run_index = (gs.run_index < runs.size()) ? gs.run_index : 0;
            gg.atlas_page_and_flags = static_cast<std::uint32_t>(gs.atlas_page) | (static_cast<std::uint32_t>(gs.flags) << 16);
            gg.atlas_pos = static_cast<std::uint32_t>(gs.atlas_x) | (static_cast<std::uint32_t>(gs.atlas_y) << 16);
            gg.atlas_size = static_cast<std::uint32_t>(gs.atlas_w) | (static_cast<std::uint32_t>(gs.atlas_h) << 16);
            gg.plane_left = gs.plane_left;
            gg.plane_top = gs.plane_top;
            gg.plane_right = (gs.plane_right > gs.plane_left) ? gs.plane_right : (gs.plane_left + gs.atlas_w);
            gg.plane_bottom = (gs.plane_bottom > gs.plane_top) ? gs.plane_bottom : (gs.plane_top + gs.atlas_h);
            gg.draw_order = gs.draw_order != 0 ? gs.draw_order : static_cast<std::uint32_t>(i);
            gg.pad0 = 0;
            gg.pad1 = 0;
            gg.pad2 = 0;

            const auto& run = gpu_runs[gg.run_index];
            const float gx0 = run.tx + gg.plane_left * run.sx;
            const float gy0 = run.ty + gg.plane_top * run.sy;
            const float gx1 = run.tx + gg.plane_right * run.sx;
            const float gy1 = run.ty + gg.plane_bottom * run.sy;

            dispatch_x0 = std::min(dispatch_x0, static_cast<std::int32_t>(std::floor(gx0)));
            dispatch_y0 = std::min(dispatch_y0, static_cast<std::int32_t>(std::floor(gy0)));
            dispatch_x1 = std::max(dispatch_x1, static_cast<std::int32_t>(std::ceil(gx1)));
            dispatch_y1 = std::max(dispatch_y1, static_cast<std::int32_t>(std::ceil(gy1)));
        }

        if (dispatch_x0 >= dispatch_x1 || dispatch_y0 >= dispatch_y1) {
            dispatch_x0 = 0;
            dispatch_y0 = 0;
            dispatch_x1 = static_cast<std::int32_t>(dst_image.width);
            dispatch_y1 = static_cast<std::int32_t>(dst_image.height);
        }

        const VkDeviceSize glyph_bytes = static_cast<VkDeviceSize>(glyphs.size() * sizeof(GpuGlyphStatic));
        const VkDeviceSize run_bytes = static_cast<VkDeviceSize>(runs.size() * sizeof(GpuTextRunDynamic));

        const std::size_t pass_slot = frame_batch.active
            ? frame_batch.next_slot * kGlyphInstancePassesPerSlot +
                (frame_batch.pass_count % kGlyphInstancePassesPerSlot)
            : 0;

        ensure_glyph_instance_buffer(glyph_bytes, pass_slot);
        ensure_text_run_dynamic_buffer(run_bytes, pass_slot);

        const VkBuffer glyph_buffer = glyph_instance_buffers[pass_slot];
        const VkBuffer run_buffer = text_run_dynamic_buffers[pass_slot];

        std::uint64_t glyph_hash = 1469598103934665603ull;
        const auto* glyph_ptr = reinterpret_cast<const std::uint8_t*>(gpu_glyphs);
        for (std::size_t b = 0; b < glyph_bytes; ++b) {
            glyph_hash ^= glyph_ptr[b];
            glyph_hash *= 1099511628211ull;
        }
        const bool glyph_updated =
            glyph_instance_sizes[pass_slot] != glyph_bytes ||
            glyph_instance_hashes[pass_slot] != glyph_hash;

        std::uint64_t run_hash = 1469598103934665603ull;
        const auto* run_ptr = reinterpret_cast<const std::uint8_t*>(gpu_runs);
        for (std::size_t b = 0; b < run_bytes; ++b) {
            run_hash ^= run_ptr[b];
            run_hash *= 1099511628211ull;
        }
        const bool run_updated =
            text_run_dynamic_sizes[pass_slot] != run_bytes ||
            text_run_dynamic_hashes[pass_slot] != run_hash;

        if (frame_batch.active) {
            const auto descriptors = allocate_pass_descriptor_set();
            write_text_batch_descriptors(descriptors, dst_image, atlas_image, glyph_buffer, run_buffer);
            const auto cmd = active_command_buffer();
            emit_pass_sync(cmd, {&dst_image, &atlas_image});
            if (glyph_updated) {
                vkCmdUpdateBuffer(cmd, glyph_buffer, 0, glyph_bytes, gpu_glyphs);
                glyph_instance_hashes[pass_slot] = glyph_hash;
                glyph_instance_sizes[pass_slot] = glyph_bytes;
            }
            if (run_updated) {
                vkCmdUpdateBuffer(cmd, run_buffer, 0, run_bytes, gpu_runs);
                text_run_dynamic_hashes[pass_slot] = run_hash;
                text_run_dynamic_sizes[pass_slot] = run_bytes;
            }
            record_text_batch(cmd, descriptors, dst_image,
                              static_cast<std::int32_t>(glyphs.size()),
                              static_cast<std::int32_t>(runs.size()),
                              glyph_buffer, glyph_updated,
                              run_buffer, run_updated,
                              dispatch_x0, dispatch_y0, dispatch_x1, dispatch_y1);
            dst_image.initialized = true;
            ++frame_batch.pass_count;
            ++stats.passes_executed;
        } else {
            ensure_descriptor_set();
            write_text_batch_descriptors(descriptor_set, dst_image, atlas_image, glyph_buffer, run_buffer);
            begin_command_buffer();
            emit_conservative_pass_sync(command_buffer, {&dst_image, &atlas_image});
            if (glyph_updated) {
                vkCmdUpdateBuffer(command_buffer, glyph_buffer, 0, glyph_bytes, gpu_glyphs);
                glyph_instance_hashes[pass_slot] = glyph_hash;
                glyph_instance_sizes[pass_slot] = glyph_bytes;
            }
            if (run_updated) {
                vkCmdUpdateBuffer(command_buffer, run_buffer, 0, run_bytes, gpu_runs);
                text_run_dynamic_hashes[pass_slot] = run_hash;
                text_run_dynamic_sizes[pass_slot] = run_bytes;
            }
            record_text_batch(command_buffer, descriptor_set, dst_image,
                              static_cast<std::int32_t>(glyphs.size()),
                              static_cast<std::int32_t>(runs.size()),
                              glyph_buffer, glyph_updated,
                              run_buffer, run_updated,
                              dispatch_x0, dispatch_y0, dispatch_x1, dispatch_y1);
            dst_image.initialized = true;
            submit();
        }
    }

    struct alignas(16) GpuLayerInstance {
        float dst_rect[4]{0.0f, 0.0f, 0.0f, 0.0f};
        float src_rect[4]{0.0f, 0.0f, 1.0f, 1.0f};
        float affine_row0[4]{0.0f, 0.0f, 0.0f, 0.0f};
        float affine_row1[4]{0.0f, 0.0f, 0.0f, 0.0f};
        float limits[4]{0.0f, 0.0f, 1.0f, 1.0f};
        std::uint32_t has_affine{0};
        std::uint32_t blend_mode{0};
        std::uint32_t resource_idx{0};
        std::uint32_t pad{0};
    };
    static_assert(sizeof(GpuLayerInstance) == 96, "GpuLayerInstance must be 96 bytes");

    // ── execute_layer_batch ──────────────────────────────────────────────
    //
    // True GPU layer-batch consumer. Every LayerInstance in `instances` is
    // composited into `destination` in painter order via a fused compute
    // kernel dispatch (1 or few dispatches per batch).
    void execute_layer_batch(runtime::RenderSurfaceHandle destination,
                             std::span<const runtime::LayerInstance> instances,
                             std::span<const runtime::RenderSurfaceHandle> resources,
                             std::span<const float> transforms,
                             std::span<const float> paints) {
        (void)paints;
        if (instances.empty()) return;
        ++stats.layer_batch_calls;
        stats.layer_instances_processed += instances.size();

        auto& dst_image = resolve_image(destination);
        if (!dst_image.initialized || dst_image.width == 0 || dst_image.height == 0) {
            throw std::invalid_argument("Vulkan layer batch references an invalid destination surface");
        }

        // Process contiguous spans of instances that share the same resource handle
        std::size_t start_idx = 0;
        while (start_idx < instances.size()) {
            const auto res_idx = instances[start_idx].resource_index;
            const auto source_handle = (res_idx < resources.size())
                ? resources[res_idx] : runtime::kInvalidRenderSurfaceHandle;

            std::size_t end_idx = start_idx + 1;
            while (end_idx < instances.size() && instances[end_idx].resource_index == res_idx) {
                ++end_idx;
            }

            auto& src_image = (source_handle != runtime::kInvalidRenderSurfaceHandle)
                ? resolve_image(source_handle) : dst_image;

            const std::size_t count = end_idx - start_idx;
            std::array<GpuLayerInstance, 512> local_instances;
            std::vector<GpuLayerInstance> heap_instances;
            GpuLayerInstance* gpu_instances = local_instances.data();
            if (count > local_instances.size()) {
                heap_instances.resize(count);
                gpu_instances = heap_instances.data();
            }

            std::int32_t dispatch_x0 = static_cast<std::int32_t>(dst_image.width);
            std::int32_t dispatch_y0 = static_cast<std::int32_t>(dst_image.height);
            std::int32_t dispatch_x1 = 0;
            std::int32_t dispatch_y1 = 0;

            for (std::size_t i = 0; i < count; ++i) {
                const auto& inst = instances[start_idx + i];
                auto& gpu_inst = gpu_instances[i];
                gpu_inst = GpuLayerInstance{};

                float dx0 = inst.dst_x0;
                float dy0 = inst.dst_y0;
                float dx1 = inst.dst_x1;
                float dy1 = inst.dst_y1;
                if (dx1 <= 1.0f && dy1 <= 1.0f && (dx1 > dx0 || dy1 > dy0)) {
                    dx0 *= static_cast<float>(dst_image.width);
                    dy0 *= static_cast<float>(dst_image.height);
                    dx1 *= static_cast<float>(dst_image.width);
                    dy1 *= static_cast<float>(dst_image.height);
                }
                gpu_inst.dst_rect[0] = dx0;
                gpu_inst.dst_rect[1] = dy0;
                gpu_inst.dst_rect[2] = dx1;
                gpu_inst.dst_rect[3] = dy1;

                gpu_inst.src_rect[0] = inst.src_x0;
                gpu_inst.src_rect[1] = inst.src_y0;
                gpu_inst.src_rect[2] = (inst.src_x1 == 0.0f && inst.src_y1 == 0.0f) ? 1.0f : inst.src_x1;
                gpu_inst.src_rect[3] = (inst.src_x1 == 0.0f && inst.src_y1 == 0.0f) ? 1.0f : inst.src_y1;

                gpu_inst.limits[0] = static_cast<float>(src_image.width);
                gpu_inst.limits[1] = static_cast<float>(src_image.height);
                gpu_inst.limits[2] = inst.opacity;
                gpu_inst.limits[3] = 1.0f; // bilinear

                if (inst.transform_index > 0 && (inst.transform_index + 1) * 16 <= transforms.size()) {
                    gpu_inst.has_affine = 1;
                    const float* m = transforms.data() + inst.transform_index * 16;
                    gpu_inst.affine_row0[0] = m[0];
                    gpu_inst.affine_row0[1] = m[4];
                    gpu_inst.affine_row0[2] = m[12];
                    gpu_inst.affine_row0[3] = 0.0f;
                    gpu_inst.affine_row1[0] = m[1];
                    gpu_inst.affine_row1[1] = m[5];
                    gpu_inst.affine_row1[2] = m[13];
                    gpu_inst.affine_row1[3] = 0.0f;
                    dispatch_x0 = 0;
                    dispatch_y0 = 0;
                    dispatch_x1 = static_cast<std::int32_t>(dst_image.width);
                    dispatch_y1 = static_cast<std::int32_t>(dst_image.height);
                } else {
                    gpu_inst.has_affine = 0;
                    dispatch_x0 = std::min(dispatch_x0, static_cast<std::int32_t>(std::floor(dx0)));
                    dispatch_y0 = std::min(dispatch_y0, static_cast<std::int32_t>(std::floor(dy0)));
                    dispatch_x1 = std::max(dispatch_x1, static_cast<std::int32_t>(std::ceil(dx1)));
                    dispatch_y1 = std::max(dispatch_y1, static_cast<std::int32_t>(std::ceil(dy1)));
                }

                gpu_inst.blend_mode = (inst.blend == BlendMode::Add) ? 1u : 0u;
                gpu_inst.resource_idx = inst.resource_index;
            }

            if (dispatch_x0 >= dispatch_x1 || dispatch_y0 >= dispatch_y1) {
                dispatch_x0 = 0;
                dispatch_y0 = 0;
                dispatch_x1 = static_cast<std::int32_t>(dst_image.width);
                dispatch_y1 = static_cast<std::int32_t>(dst_image.height);
            }

            const VkDeviceSize bytes = static_cast<VkDeviceSize>(count * sizeof(GpuLayerInstance));
            const std::size_t instance_slot = frame_batch.active
                ? frame_batch.next_slot * kGlyphInstancePassesPerSlot +
                    (frame_batch.pass_count % kGlyphInstancePassesPerSlot)
                : 0;
            ensure_layer_instance_buffer(bytes, instance_slot);
            const VkBuffer instance_buffer = layer_instance_buffers[instance_slot];

            std::uint64_t instance_hash = 1469598103934665603ull;
            const auto* byte_ptr = reinterpret_cast<const std::uint8_t*>(gpu_instances);
            for (std::size_t b = 0; b < bytes; ++b) {
                instance_hash ^= byte_ptr[b];
                instance_hash *= 1099511628211ull;
            }
            const bool instance_updated =
                layer_instance_sizes[instance_slot] != bytes ||
                layer_instance_hashes[instance_slot] != instance_hash;

            if (frame_batch.active) {
                const auto descriptors = allocate_pass_descriptor_set();
                write_layer_batch_descriptors(descriptors, dst_image, src_image, instance_buffer);
                const auto cmd = active_command_buffer();
                emit_pass_sync(cmd, {&dst_image, &src_image});
                if (instance_updated) {
                    vkCmdUpdateBuffer(cmd, instance_buffer, 0, bytes, gpu_instances);
                    layer_instance_hashes[instance_slot] = instance_hash;
                    layer_instance_sizes[instance_slot] = bytes;
                }
                record_layer_batch(cmd, descriptors, dst_image, static_cast<std::int32_t>(count),
                                   instance_buffer, instance_updated,
                                   dispatch_x0, dispatch_y0, dispatch_x1, dispatch_y1);
                dst_image.initialized = true;
                ++frame_batch.pass_count;
                ++stats.passes_executed;
            } else {
                ensure_descriptor_set();
                write_layer_batch_descriptors(descriptor_set, dst_image, src_image, instance_buffer);
                begin_command_buffer();
                emit_conservative_pass_sync(command_buffer, {&dst_image, &src_image});
                if (instance_updated) {
                    vkCmdUpdateBuffer(command_buffer, instance_buffer, 0, bytes, gpu_instances);
                    layer_instance_hashes[instance_slot] = instance_hash;
                    layer_instance_sizes[instance_slot] = bytes;
                }
                record_layer_batch(command_buffer, descriptor_set, dst_image, static_cast<std::int32_t>(count),
                                   instance_buffer, instance_updated,
                                   dispatch_x0, dispatch_y0, dispatch_x1, dispatch_y1);
                dst_image.initialized = true;
                submit();
            }

            start_idx = end_idx;
        }
    }

    void ensure_images(std::uint32_t width, std::uint32_t height) {
        if (dst.width == width && dst.height == height && src.image != VK_NULL_HANDLE) return;
        destroy_image(dst);
        destroy_image(src);
        check(vkResetDescriptorPool(device, descriptor_pool, 0), "vkResetDescriptorPool");
        make_image(dst, width, height);
        make_image(src, width, height);
        descriptor_set = VK_NULL_HANDLE;
        glow_descriptor_sets = {};
        bind_descriptors(dst, src);
    }

    void ensure_staging(VkDeviceSize bytes) {
        if (staging_capacity >= bytes) return;
        if (staging != VK_NULL_HANDLE) vkDestroyBuffer(device, staging, nullptr);
        if (staging_memory != VK_NULL_HANDLE) vkFreeMemory(device, staging_memory, nullptr);
        const VkBufferCreateInfo info{
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, bytes,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_SHARING_MODE_EXCLUSIVE, 0, nullptr};
        check(vkCreateBuffer(device, &info, nullptr, &staging), "vkCreateBuffer(staging)");
        ++stats.staging_allocations;
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, staging, &requirements);
        const VkMemoryAllocateInfo allocation{
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, requirements.size,
            memory_type(requirements.memoryTypeBits,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
        check(vkAllocateMemory(device, &allocation, nullptr, &staging_memory), "vkAllocateMemory(staging)");
        check(vkBindBufferMemory(device, staging, staging_memory, 0), "vkBindBufferMemory");
        staging_capacity = bytes;
    }

    // Grow (or create) the device-local glyph-instance storage buffer.  The
    // buffer is updated in-band with vkCmdUpdateBuffer and read by the
    // text-run compute kernel, so it needs both transfer-dst and storage
    // usage; device-local memory keeps the GPU read path on-card.
    void ensure_glyph_instance_buffer(VkDeviceSize bytes, std::size_t index) {
        if (index >= kGlyphInstanceRingSize) index = 0;
        if (glyph_instance_capacities[index] >= bytes) return;
        if (glyph_instance_buffers[index] != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, glyph_instance_buffers[index], nullptr);
        }
        if (glyph_instance_memories[index] != VK_NULL_HANDLE) {
            vkFreeMemory(device, glyph_instance_memories[index], nullptr);
        }
        const VkBufferCreateInfo info{
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, bytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_SHARING_MODE_EXCLUSIVE, 0, nullptr};
        check(vkCreateBuffer(device, &info, nullptr, &glyph_instance_buffers[index]),
              "vkCreateBuffer(glyph instances)");
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, glyph_instance_buffers[index], &requirements);
        const VkMemoryAllocateInfo allocation{
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, requirements.size,
            memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
        check(vkAllocateMemory(device, &allocation, nullptr, &glyph_instance_memories[index]),
              "vkAllocateMemory(glyph instances)");
        check(vkBindBufferMemory(device, glyph_instance_buffers[index], glyph_instance_memories[index], 0),
              "vkBindBufferMemory(glyph instances)");
        glyph_instance_capacities[index] = bytes;
        glyph_instance_hashes[index] = 0;
        glyph_instance_sizes[index] = 0;
    }

    void ensure_layer_instance_buffer(VkDeviceSize bytes, std::size_t index) {
        if (index >= kGlyphInstanceRingSize) index = 0;
        if (layer_instance_capacities[index] >= bytes) return;
        if (layer_instance_buffers[index] != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, layer_instance_buffers[index], nullptr);
        }
        if (layer_instance_memories[index] != VK_NULL_HANDLE) {
            vkFreeMemory(device, layer_instance_memories[index], nullptr);
        }
        const VkBufferCreateInfo info{
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, bytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_SHARING_MODE_EXCLUSIVE, 0, nullptr};
        check(vkCreateBuffer(device, &info, nullptr, &layer_instance_buffers[index]),
              "vkCreateBuffer(layer instances)");
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, layer_instance_buffers[index], &requirements);
        const VkMemoryAllocateInfo allocation{
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, requirements.size,
            memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
        check(vkAllocateMemory(device, &allocation, nullptr, &layer_instance_memories[index]),
              "vkAllocateMemory(layer instances)");
        check(vkBindBufferMemory(device, layer_instance_buffers[index], layer_instance_memories[index], 0),
              "vkBindBufferMemory(layer instances)");
        layer_instance_capacities[index] = bytes;
        layer_instance_hashes[index] = 0;
        layer_instance_sizes[index] = 0;
    }

    void ensure_text_run_dynamic_buffer(VkDeviceSize bytes, std::size_t index) {
        if (index >= kGlyphInstanceRingSize) index = 0;
        if (text_run_dynamic_capacities[index] >= bytes) return;
        if (text_run_dynamic_buffers[index] != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, text_run_dynamic_buffers[index], nullptr);
        }
        if (text_run_dynamic_memories[index] != VK_NULL_HANDLE) {
            vkFreeMemory(device, text_run_dynamic_memories[index], nullptr);
        }
        const VkBufferCreateInfo info{
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, bytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_SHARING_MODE_EXCLUSIVE, 0, nullptr};
        check(vkCreateBuffer(device, &info, nullptr, &text_run_dynamic_buffers[index]),
              "vkCreateBuffer(text run dynamic)");
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, text_run_dynamic_buffers[index], &requirements);
        const VkMemoryAllocateInfo allocation{
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, requirements.size,
            memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
        check(vkAllocateMemory(device, &allocation, nullptr, &text_run_dynamic_memories[index]),
              "vkAllocateMemory(text run dynamic)");
        check(vkBindBufferMemory(device, text_run_dynamic_buffers[index], text_run_dynamic_memories[index], 0),
              "vkBindBufferMemory(text run dynamic)");
        text_run_dynamic_capacities[index] = bytes;
        text_run_dynamic_hashes[index] = 0;
        text_run_dynamic_sizes[index] = 0;
    }

    static void transition(VkCommandBuffer command, VkImage image,
                           VkImageLayout old_layout, VkImageLayout new_layout) {
        const VkImageMemoryBarrier barrier{
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr,
            VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            old_layout, new_layout, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
            image, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    void begin_command_buffer() {
        wait_for_pending();
        const VkCommandBufferBeginInfo begin{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr};
        check(vkResetCommandBuffer(command_buffer, 0), "vkResetCommandBuffer");
        check(vkBeginCommandBuffer(command_buffer, &begin), "vkBeginCommandBuffer");
    }

    void wait_for_pending() {
        if (pending_timeline_value != 0) {
            const auto wait_start = profiling::now();
            check(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
            const auto wait_us = static_cast<std::uint64_t>(profiling::elapsed_us(wait_start));
            stats.gpu_wait_cpu_us += wait_us;
            ++stats.standalone_wait_count;
            stats.standalone_wait_us += wait_us;
            check(vkResetFences(device, 1, &fence), "vkResetFences");
            pending_timeline_value = 0;
        }
        // Synchronize any frame-batch slots still in flight.  This is a sync
        // point (release/readback path), distinct from begin_frame_batch()
        // which waits ONLY on the fence of the slot being reused.
        for (std::size_t i = 0; i < FrameBatchState::kSlotCount; ++i) {
            if (!frame_batch.in_flight[i]) continue;
            const auto wait_start = profiling::now();
            const VkResult wait_result = vkWaitForFences(
                device, 1, &frame_batch.fences[i], VK_TRUE, UINT64_MAX);
            if (wait_result == VK_ERROR_DEVICE_LOST) {
                spdlog::error(
                    "[vulkan] DEVICE LOST REPORT phase=wait_for_pending slot={} "
                    "in_flight_slots=[{},{},{}] pending_timeline={}",
                    i, frame_batch.in_flight[0], frame_batch.in_flight[1],
                    frame_batch.in_flight[2], pending_timeline_value);
            }
            check(wait_result, "vkWaitForFences(frame batch slot)");
            const auto wait_us = static_cast<std::uint64_t>(profiling::elapsed_us(wait_start));
            stats.gpu_wait_cpu_us += wait_us;
            ++stats.frame_batch_drain_wait_count;
            stats.frame_batch_drain_wait_us += wait_us;
            check(vkResetFences(device, 1, &frame_batch.fences[i]),
                  "vkResetFences(frame batch slot)");
            frame_batch.in_flight[i] = false;
            read_gpu_timestamps(i);
        }
        // Synchronize any in-flight upload slots too.  release_surface() and
        // every standalone path call wait_for_pending() before reusing or
        // destroying a surface, and an asynchronous upload may still be
        // copying into it; draining these slots closes the use-after-free
        // where an in-flight VkImage was destroyed before its copy completed.
        for (auto& slot : upload_slots) {
            wait_upload_slot(slot);
        }
    }

    std::uint64_t submit(bool wait_for_completion = true) {
        check(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer");
        const auto signal_value = ++next_timeline_value;
        std::vector<VkSemaphore> wait_semaphores;
        std::vector<VkPipelineStageFlags> wait_stages;
        std::vector<VkSemaphore> signal_semaphores{timeline_semaphore};
        std::vector<std::uint64_t> signal_values{signal_value};
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
        for (const auto physical_slot : cuda_ready_surfaces) {
            const auto it = physical_surfaces.find(physical_slot);
            if (it == physical_surfaces.end()) continue;
            wait_semaphores.push_back(it->second.image.cuda_to_vulkan);
            wait_stages.push_back(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            signal_semaphores.push_back(it->second.image.vulkan_to_cuda);
            signal_values.push_back(0);
        }
        for (const auto physical_slot : cuda_export_ready_surfaces) {
            if (cuda_ready_surfaces.contains(physical_slot)) continue;
            const auto it = physical_surfaces.find(physical_slot);
            if (it == physical_surfaces.end()) continue;
            signal_semaphores.push_back(it->second.image.vulkan_to_cuda);
            signal_values.push_back(0);
        }
#endif
        const VkTimelineSemaphoreSubmitInfo timeline_submit{
            VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO, nullptr,
            0, nullptr, static_cast<std::uint32_t>(signal_values.size()),
            signal_values.data()};
        const VkSubmitInfo submit_info{
            VK_STRUCTURE_TYPE_SUBMIT_INFO, &timeline_submit,
            static_cast<std::uint32_t>(wait_semaphores.size()),
            wait_semaphores.data(), wait_stages.data(), 1, &command_buffer,
            static_cast<std::uint32_t>(signal_semaphores.size()),
            signal_semaphores.data()};
        const auto submit_start = profiling::now();
        check(vkQueueSubmit(queue, 1, &submit_info, fence), "vkQueueSubmit");
        stats.gpu_submit_cpu_us += static_cast<std::uint64_t>(profiling::elapsed_us(submit_start));
        ++stats.submissions;
        pending_timeline_value = signal_value;
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
        cuda_ready_surfaces.clear();
        cuda_export_ready_surfaces.clear();
#endif
        if (wait_for_completion) wait_for_pending();
        return signal_value;
    }

    void composite(Framebuffer& destination, const Framebuffer& source) {
        const auto width = static_cast<std::uint32_t>(destination.width());
        const auto height = static_cast<std::uint32_t>(destination.height());
        const VkDeviceSize image_bytes = static_cast<VkDeviceSize>(width) * height * sizeof(float) * 4;
        ensure_images(width, height);
        ensure_staging(image_bytes * 3);

        std::vector<float> packed(static_cast<std::size_t>(width) * height * 8);
        auto pack = [&](const Framebuffer& framebuffer, std::size_t offset) {
            std::size_t index = offset / sizeof(float);
            for (int y = 0; y < framebuffer.height(); ++y) {
                for (int x = 0; x < framebuffer.width(); ++x) {
                    const auto color = framebuffer.get_pixel(x, y);
                    packed[index++] = color.r;
                    packed[index++] = color.g;
                    packed[index++] = color.b;
                    packed[index++] = color.a;
                }
            }
        };
        pack(source, 0);
        pack(destination, static_cast<std::size_t>(image_bytes));
        void* mapped = nullptr;
        check(vkMapMemory(device, staging_memory, 0, image_bytes * 2, 0, &mapped), "vkMapMemory(upload)");
        std::memcpy(mapped, packed.data(), static_cast<std::size_t>(image_bytes * 2));
        vkUnmapMemory(device, staging_memory);

        const VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
                                             VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr};
        check(vkResetCommandBuffer(command_buffer, 0), "vkResetCommandBuffer");
        check(vkBeginCommandBuffer(command_buffer, &begin), "vkBeginCommandBuffer");
        transition(command_buffer, src.image,
                   src.initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        transition(command_buffer, dst.image,
                   dst.initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        const VkBufferImageCopy source_copy{0, width, height, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {width, height, 1}};
        const VkBufferImageCopy destination_copy{image_bytes, width, height, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {width, height, 1}};
        vkCmdCopyBufferToImage(command_buffer, staging, src.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &source_copy);
        vkCmdCopyBufferToImage(command_buffer, staging, dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &destination_copy);
        transition(command_buffer, src.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
        transition(command_buffer, dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernel_registry.resolve(GpuKernelId::Composite)));
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1, &descriptor_set, 0, nullptr);
        vkCmdDispatch(command_buffer, (width + 15) / 16, (height + 15) / 16, 1);
        transition(command_buffer, dst.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        const VkBufferImageCopy output_copy{image_bytes * 2, width, height, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {width, height, 1}};
        vkCmdCopyImageToBuffer(command_buffer, dst.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging, 1, &output_copy);
        transition(command_buffer, dst.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
        src.initialized = true;
        dst.initialized = true;
        submit();

        check(vkMapMemory(device, staging_memory, image_bytes * 2, image_bytes, 0, &mapped), "vkMapMemory(readback)");
        const float* output = static_cast<const float*>(mapped);
        for (int y = 0; y < destination.height(); ++y) {
            for (int x = 0; x < destination.width(); ++x) {
                const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 4;
                destination.set_pixel(x, y, Color{output[index], output[index + 1], output[index + 2], output[index + 3]});
            }
        }
        vkUnmapMemory(device, staging_memory);
    }
};
