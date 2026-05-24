import sys

old_str = """        if (use_dirty_rects) {
            bool camera_changed = !sw_renderer->m_prev_camera_valid ||
                                   sw_renderer->m_prev_camera.enabled != cam25d.enabled ||
                                   (cam25d.enabled && (
                                       sw_renderer->m_prev_camera.position != cam25d.position ||
                                       sw_renderer->m_prev_camera.zoom != cam25d.zoom ||
                                       sw_renderer->m_prev_camera.fov_deg != cam25d.fov_deg ||
                                       sw_renderer->m_prev_camera.rotation != cam25d.rotation ||
                                       sw_renderer->m_prev_camera.projection_mode != cam25d.projection_mode
                                   ));

            raster::BBox union_dirty{0, 0, 0, 0};
            bool has_dirty = false;
            auto add_dirty_bbox = [&](const raster::BBox& b) {
                if (b.is_empty()) return;
                raster::BBox clipped = b;
                clipped.clip_to(width, height);
                if (clipped.is_empty()) return;
                if (!has_dirty) {
                    union_dirty = clipped;
                    has_dirty = true;
                } else {
                    union_dirty.x0 = std::min(union_dirty.x0, clipped.x0);
                    union_dirty.y0 = std::min(union_dirty.y0, clipped.y0);
                    union_dirty.x1 = std::max(union_dirty.x1, clipped.x1);
                    union_dirty.y1 = std::max(union_dirty.y1, clipped.y1);
                }
            };

            auto same_bbox = [](const raster::BBox& a, const raster::BBox& b) {
                return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1;
            };

            auto add_layer_dirty = [&](const SoftwareRenderer::LayerBBoxState& curr,
                                       const SoftwareRenderer::LayerBBoxState* prev) {
                const bool prev_exists = prev != nullptr;
                const bool curr_visible = curr.visible;
                const bool prev_visible = prev_exists ? prev->visible : false;

                if (!prev_exists) {
                    add_dirty_bbox(curr.bbox);
                    return;
                }

                if (curr_visible != prev_visible) {
                    add_dirty_bbox(curr_visible ? curr.bbox : prev->bbox);
                    return;
                }

                if (!curr_visible) {
                    return;
                }

                const bool geometry_changed =
                    (camera_changed && curr.is_3d) ||
                    (curr.world_matrix != prev->world_matrix);
                const bool content_changed =
                    !curr.cache_static ||
                    curr.opacity != prev->opacity;

                if (geometry_changed) {
                    if (same_bbox(curr.bbox, prev->bbox)) {
                        add_dirty_bbox(curr.bbox);
                    } else {
                        add_dirty_bbox(curr.bbox);
                        add_dirty_bbox(prev->bbox);
                    }
                    return;
                }

                if (content_changed) {
                    add_dirty_bbox(curr.bbox);
                }
            };

            for (const auto& pair : current_layer_bboxes) {
                const auto& name = pair.first;
                const auto& curr = pair.second;
                auto prev_it = sw_renderer->m_prev_layer_bboxes.find(name);
                add_layer_dirty(
                    curr,
                    prev_it == sw_renderer->m_prev_layer_bboxes.end()
                        ? nullptr
                        : &prev_it->second
                );
            }
            for (const auto& pair : sw_renderer->m_prev_layer_bboxes) {
                if (current_layer_bboxes.find(pair.first) == current_layer_bboxes.end()) {
                    add_dirty_bbox(pair.second.bbox);
                }
            }

            if (has_dirty) {
                dirty_rect = union_dirty;
            } else {
                dirty_rect = raster::BBox{0, 0, 0, 0};
            }

            double dirty_area = static_cast<double>(union_dirty.x1 - union_dirty.x0) * (union_dirty.y1 - union_dirty.y0);
            double total_area = static_cast<double>(width) * height;
            sw_renderer->m_last_dirty_area_ratio = has_dirty ? (dirty_area / total_area) : 0.0;

            if (ctx.counters) {
                uint64_t union_pixels = has_dirty ? static_cast<uint64_t>(std::max(0, union_dirty.x1 - union_dirty.x0)) * std::max(0, union_dirty.y1 - union_dirty.y0) : 0;
                ctx.counters->dirty_union_area_pixels.store(union_pixels, std::memory_order_relaxed);
            }
        }"""

new_str = """        if (use_dirty_rects) {
            bool camera_changed = !sw_renderer->m_prev_camera_valid ||
                                   sw_renderer->m_prev_camera.enabled != cam25d.enabled ||
                                   (cam25d.enabled && (
                                       sw_renderer->m_prev_camera.position != cam25d.position ||
                                       sw_renderer->m_prev_camera.zoom != cam25d.zoom ||
                                       sw_renderer->m_prev_camera.fov_deg != cam25d.fov_deg ||
                                       sw_renderer->m_prev_camera.rotation != cam25d.rotation ||
                                       sw_renderer->m_prev_camera.projection_mode != cam25d.projection_mode
                                   ));

            raster::DirtyRectMask dirty_mask(width, height);
            raster::BBox union_dirty{0, 0, 0, 0};
            bool has_dirty = false;
            auto add_dirty_bbox = [&](const raster::BBox& b) {
                if (b.is_empty()) return;
                raster::BBox clipped = b;
                clipped.clip_to(width, height);
                if (clipped.is_empty()) return;
                
                if (settings.enable_dirty_bitmask) {
                    dirty_mask.mark_dirty(clipped);
                }

                if (!has_dirty) {
                    union_dirty = clipped;
                    has_dirty = true;
                } else {
                    union_dirty.x0 = std::min(union_dirty.x0, clipped.x0);
                    union_dirty.y0 = std::min(union_dirty.y0, clipped.y0);
                    union_dirty.x1 = std::max(union_dirty.x1, clipped.x1);
                    union_dirty.y1 = std::max(union_dirty.y1, clipped.y1);
                }
            };

            auto same_bbox = [](const raster::BBox& a, const raster::BBox& b) {
                return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1;
            };

            auto add_layer_dirty = [&](const SoftwareRenderer::LayerBBoxState& curr,
                                       const SoftwareRenderer::LayerBBoxState* prev) {
                const bool prev_exists = prev != nullptr;
                const bool curr_visible = curr.visible;
                const bool prev_visible = prev_exists ? prev->visible : false;

                if (!prev_exists) {
                    add_dirty_bbox(curr.bbox);
                    return;
                }

                if (curr_visible != prev_visible) {
                    add_dirty_bbox(curr_visible ? curr.bbox : prev->bbox);
                    return;
                }

                if (!curr_visible) {
                    return;
                }

                const bool geometry_changed =
                    (camera_changed && curr.is_3d) ||
                    (curr.world_matrix != prev->world_matrix);
                const bool content_changed =
                    !curr.cache_static ||
                    curr.opacity != prev->opacity;

                if (geometry_changed) {
                    if (same_bbox(curr.bbox, prev->bbox)) {
                        add_dirty_bbox(curr.bbox);
                    } else {
                        add_dirty_bbox(curr.bbox);
                        add_dirty_bbox(prev->bbox);
                    }
                    return;
                }

                if (content_changed) {
                    add_dirty_bbox(curr.bbox);
                }
            };

            for (const auto& pair : current_layer_bboxes) {
                const auto& name = pair.first;
                const auto& curr = pair.second;
                auto prev_it = sw_renderer->m_prev_layer_bboxes.find(name);
                add_layer_dirty(
                    curr,
                    prev_it == sw_renderer->m_prev_layer_bboxes.end()
                        ? nullptr
                        : &prev_it->second
                );
            }
            for (const auto& pair : sw_renderer->m_prev_layer_bboxes) {
                if (current_layer_bboxes.find(pair.first) == current_layer_bboxes.end()) {
                    add_dirty_bbox(pair.second.bbox);
                }
            }

            if (has_dirty) {
                if (settings.enable_dirty_bitmask && !dirty_mask.is_empty()) {
                    ctx.dirty_mask = dirty_mask;
                    // Provide a fast union bound to quickly reject nodes completely outside.
                    ctx.dirty_rect = union_dirty;
                } else {
                    ctx.dirty_rect = union_dirty;
                }
            } else {
                ctx.dirty_rect = raster::BBox{0, 0, 0, 0};
            }

            double dirty_area_ratio = 0.0;
            if (has_dirty) {
                if (settings.enable_dirty_bitmask) {
                    int dirty_tiles = 0;
                    for (int ty = 0; ty < dirty_mask.tiles_y(); ++ty) {
                        for (int tx = 0; tx < dirty_mask.tiles_x(); ++tx) {
                            if (dirty_mask.is_tile_dirty(tx, ty)) {
                                dirty_tiles++;
                            }
                        }
                    }
                    double total_tiles = static_cast<double>(dirty_mask.tiles_x() * dirty_mask.tiles_y());
                    dirty_area_ratio = dirty_tiles / std::max(1.0, total_tiles);
                } else {
                    double dirty_area = static_cast<double>(union_dirty.x1 - union_dirty.x0) * (union_dirty.y1 - union_dirty.y0);
                    double total_area = static_cast<double>(width) * height;
                    dirty_area_ratio = dirty_area / total_area;
                }
            }
            sw_renderer->m_last_dirty_area_ratio = dirty_area_ratio;

            if (ctx.counters) {
                uint64_t dirty_pixels = 0;
                if (settings.enable_dirty_bitmask && has_dirty) {
                    for (int ty = 0; ty < dirty_mask.tiles_y(); ++ty) {
                        for (int tx = 0; tx < dirty_mask.tiles_x(); ++tx) {
                            if (dirty_mask.is_tile_dirty(tx, ty)) {
                                int tw = std::min(raster::DirtyRectMask::k_tile_size, width - tx * raster::DirtyRectMask::k_tile_size);
                                int th = std::min(raster::DirtyRectMask::k_tile_size, height - ty * raster::DirtyRectMask::k_tile_size);
                                dirty_pixels += tw * th;
                            }
                        }
                    }
                } else {
                    dirty_pixels = has_dirty ? static_cast<uint64_t>(std::max(0, union_dirty.x1 - union_dirty.x0)) * std::max(0, union_dirty.y1 - union_dirty.y0) : 0;
                }
                ctx.counters->dirty_union_area_pixels.store(dirty_pixels, std::memory_order_relaxed);
            }
        }"""

with open('src/render_graph/render_pipeline_scene.cpp', 'r') as f:
    content = f.read()

content = content.replace(old_str, new_str)

with open('src/render_graph/render_pipeline_scene.cpp', 'w') as f:
    f.write(content)
