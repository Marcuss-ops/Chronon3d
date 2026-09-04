namespace {

void execute_fused_batch(
    ExecutionState& state,
    RenderGraph& graph,
    RenderGraphContext& ctx,
    const CompiledLayerBatch& batch,
    GraphNodeId id,
    FramebufferPool* parent_pool,
    RenderCounters* parent_counters,
    const CompiledFrameGraph& compiled) {
    (void)parent_pool;
    if (!ctx.services.backend || batch.instances.empty()) return;

    bool is_text_batch = false;
    for (const auto& inst : batch.instances) {
        if (inst.node < compiled.nodes.size() && compiled.nodes[inst.node].kind == RenderGraphNodeKind::TextRun) {
            is_text_batch = true;
            break;
        }
    }

    if (is_text_batch) {
#ifdef CHRONON3D_ENABLE_TEXT
        std::vector<runtime::GlyphStatic> all_glyphs;
        std::vector<runtime::TextRunDynamic> all_runs;
        std::vector<runtime::RenderSurfaceHandle> atlas_pages;
        std::unordered_map<runtime::RenderSurfaceHandle, uint32_t> atlas_map;

        for (const auto& inst : batch.instances) {
            if (inst.node >= graph.size() || !graph.has_node(inst.node)) continue;
            const auto& node = graph.node(inst.node);
            if (node.kind() != RenderGraphNodeKind::TextRun) continue;
            const auto& tr_node = static_cast<const TextRunNode&>(node);
            if (!tr_node.shape()) continue;

            float tx = 0.0f, ty = 0.0f, sx = 1.0f, sy = 1.0f;
            float opacity = inst.opacity;
            if (inst.transform_index != 0 && inst.transform_index < graph.size()) {
                const auto& xform = static_cast<const TransformNode&>(graph.node(inst.transform_index));
                const auto m = xform.matrix();
                tx = m[3][0];
                ty = m[3][1];
                sx = m[0][0];
                sy = m[1][1];
                opacity *= xform.opacity();
            }

            TextRunShape local_shape = text_run::prepare_per_frame_shape(*tr_node.shape(), ctx.frame_input.sample_time);
            if (!local_shape.layout) continue;

            const auto& layout = *local_shape.layout;
            const int font_size = std::max(1, static_cast<int>(std::lround(layout.font_size)));

            runtime::TextRunDynamic run_dyn{};
            run_dyn.tx = tx;
            run_dyn.ty = ty;
            run_dyn.sx = sx;
            run_dyn.sy = sy;
            run_dyn.opacity = opacity;
            run_dyn.color = 0xFFFFFFFF;
            all_runs.push_back(run_dyn);

            for (std::size_t gi = 0; gi < layout.placed.glyphs.size(); ++gi) {
                const auto& gstate = local_shape.glyphs[gi];
                if (gstate.glyph_id == 0) continue;
                const auto& placed = layout.placed.glyphs[gi];
                if (placed.bbox_x1 <= placed.bbox_x0) continue;

                if (!ctx.services.text_render_resources) continue;
                auto entry = ctx.services.text_render_resources->lookup_glyph_atlas(
                    layout.font.font_path, gstate.glyph_id, static_cast<u32>(font_size));
                if (!entry || !entry->image) continue;

                runtime::RenderSurfaceHandle page_handle = 1;
                uint32_t page_idx = 0;
                auto it = atlas_map.find(page_handle);
                if (it != atlas_map.end()) {
                    page_idx = it->second;
                } else {
                    page_idx = static_cast<uint32_t>(atlas_pages.size());
                    atlas_pages.push_back(page_handle);
                    atlas_map[page_handle] = page_idx;
                }

                runtime::GlyphStatic g_stat{};
                g_stat.run_index = static_cast<uint32_t>(all_runs.size() - 1);
                g_stat.atlas_page = static_cast<uint16_t>(page_idx);
                g_stat.flags = 0;
                g_stat.atlas_x = static_cast<uint16_t>(entry->x_offset >= 0 ? entry->x_offset : 0);
                g_stat.atlas_y = static_cast<uint16_t>(entry->y_offset >= 0 ? entry->y_offset : 0);
                g_stat.atlas_w = static_cast<uint16_t>(entry->image->width());
                g_stat.atlas_h = static_cast<uint16_t>(entry->image->height());
                g_stat.plane_left = static_cast<float>(placed.x);
                g_stat.plane_top = static_cast<float>(placed.y);
                g_stat.plane_right = static_cast<float>(placed.x + entry->image->width());
                g_stat.plane_bottom = static_cast<float>(placed.y + entry->image->height());
                g_stat.draw_order = static_cast<uint32_t>(all_glyphs.size());
                all_glyphs.push_back(g_stat);
            }
        }

        auto dest_fb = ctx.acquire_framebuffer(ctx.frame_input.width, ctx.frame_input.height, true);
        ensure_native_surface(ctx, *dest_fb, "execute_fused_batch.text");
        ctx.services.backend->draw_text_batch(dest_fb->surface_handle(), all_glyphs, all_runs, atlas_pages);

        state.temp[id] = dest_fb;
        state.resolved_bboxes[id] = raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height};
        if (parent_counters) {
            parent_counters->nodes_executed.fetch_add(1, std::memory_order_relaxed);
        }
#endif
        return;
    }

    runtime::GpuLayerBatch gpu_batch;
    gpu_batch.output_physical_slot = batch.output_physical_slot;
    gpu_batch.is_gpu_fused = batch.is_gpu_fused;
    auto& instances = gpu_batch.instances;
    auto& resources = gpu_batch.resources;
    std::unordered_map<runtime::RenderSurfaceHandle, uint32_t> resource_map;
    std::optional<raster::BBox> image_bbox;

    for (const auto& inst : batch.instances) {
        if (inst.node >= graph.size() || !graph.has_node(inst.node)) {
            throw std::runtime_error("image batch references an invalid graph node");
        }
        const auto& node = graph.node(inst.node);
        if (node.kind() != RenderGraphNodeKind::Source) {
            throw std::runtime_error("image batch contains a non-source node");
        }
        const auto& src_node = static_cast<const SourceNode&>(node);
        if (src_node.render_node().shape.type() != ShapeType::Image) {
            throw std::runtime_error("image batch contains a non-image source");
        }
        const auto& img = src_node.render_node().shape.image();
        if (img.path.empty()) {
            throw std::runtime_error("image batch source has an empty asset path");
        }
        if (!ctx.services.image_cache) {
            throw std::runtime_error("image batch requires image_cache");
        }
        if (!ctx.services.gpu_asset_cache) {
            throw std::runtime_error("image batch requires gpu_asset_cache");
        }

        const auto cached = ctx.services.image_cache->find(img.path, img.decode_options);
        if (!cached || !cached->valid() || cached->gpu_rgba.empty() || !cached->fb_img) {
            throw std::runtime_error("image asset unavailable for GPU batch: " + img.path);
        }

        const auto& key = cached->gpu_key;
        const runtime::SurfaceDesc desc{
            key.width, key.height, key.format,
            runtime::ResourceUsage::Storage,
            runtime::LifetimeClass::JobPersistent,
            cached->gpu_rgba.size() * sizeof(float)};
        const auto acquired = ctx.services.gpu_asset_cache->acquire(key, desc, cached->gpu_rgba);
        if (!acquired.ok()) {
            throw std::runtime_error("unable to acquire GPU image surface '" +
                                     img.path + "': " + acquired.error);
        }

        runtime::RenderSurfaceHandle handle = acquired.handle;
        uint32_t res_idx = 0;
        auto it = resource_map.find(handle);
        if (it != resource_map.end()) {
            res_idx = it->second;
        } else {
            res_idx = static_cast<uint32_t>(resources.size());
            resources.push_back(handle);
            resource_map[handle] = res_idx;
        }

        float tx = 0.0f, ty = 0.0f;
        float opacity = inst.opacity;
        if (inst.transform_index != 0 && inst.transform_index < graph.size()) {
            const auto& xform = static_cast<const TransformNode&>(graph.node(inst.transform_index));
            const auto m = xform.matrix();
            tx = m[3][0];
            ty = m[3][1];
            opacity *= xform.opacity();
        }

        const Vec2 original_source_size{static_cast<float>(cached->fb_img->width()), static_cast<float>(cached->fb_img->height())};
        const auto placement = compute_media_placement(original_source_size, img.size, img.fit, img.focal_point);

        const float world_x0 = tx - img.size.x * 0.5f + placement.dst_rect.origin.x + (ctx.frame_input.width * 0.5f);
        const float world_y0 = ty - img.size.y * 0.5f + placement.dst_rect.origin.y + (ctx.frame_input.height * 0.5f);
        const float world_x1 = world_x0 + placement.dst_rect.size.x;
        const float world_y1 = world_y0 + placement.dst_rect.size.y;

        runtime::LayerInstance gpu_inst{};
        gpu_inst.resource_index = res_idx;
        gpu_inst.dst_x0 = world_x0;
        gpu_inst.dst_y0 = world_y0;
        gpu_inst.dst_x1 = world_x1;
        gpu_inst.dst_y1 = world_y1;
        // layer_batch.comp samples src_rect as normalized coordinates (0..1),
        // while media placement is expressed in source pixels. Convert at
        // this boundary so the Vulkan fused image path samples the authored
        // image instead of clamping every lookup to an edge texel.
        const float source_width = original_source_size.x;
        const float source_height = original_source_size.y;
        gpu_inst.src_x0 = placement.src_rect.origin.x / source_width;
        gpu_inst.src_y0 = placement.src_rect.origin.y / source_height;
        gpu_inst.src_x1 = (placement.src_rect.origin.x + placement.src_rect.size.x) /
                          source_width;
        gpu_inst.src_y1 = (placement.src_rect.origin.y + placement.src_rect.size.y) /
                          source_height;
        gpu_inst.opacity = opacity;
        gpu_inst.blend = BlendMode::Normal;
        gpu_inst.kind = runtime::PrimitiveKind::Image;
        instances.push_back(gpu_inst);

        raster::BBox bbox{
            static_cast<i32>(std::floor(world_x0)),
            static_cast<i32>(std::floor(world_y0)),
            static_cast<i32>(std::ceil(world_x1)),
            static_cast<i32>(std::ceil(world_y1))};
        bbox.clip_to(ctx.frame_input.width, ctx.frame_input.height);
        if (!image_bbox) image_bbox = bbox;
        else {
            image_bbox->x0 = std::min(image_bbox->x0, bbox.x0);
            image_bbox->y0 = std::min(image_bbox->y0, bbox.y0);
            image_bbox->x1 = std::max(image_bbox->x1, bbox.x1);
            image_bbox->y1 = std::max(image_bbox->y1, bbox.y1);
        }
    }

    if (instances.empty()) {
        throw std::runtime_error("image batch materialized zero GPU instances");
    }

    auto dest_fb = ctx.acquire_framebuffer(ctx.frame_input.width, ctx.frame_input.height, true);
    ensure_native_surface(ctx, *dest_fb, "execute_fused_batch.image");
    ctx.services.backend->execute_layer_batch(
        dest_fb->surface_handle(), gpu_batch, resources, {}, {});
    dest_fb->mark_gpu_authoritative();

    state.temp[id] = dest_fb;
    state.resolved_bboxes[id] = image_bbox;
    if (parent_counters) {
        parent_counters->nodes_executed.fetch_add(1, std::memory_order_relaxed);
    }
}

} // namespace
