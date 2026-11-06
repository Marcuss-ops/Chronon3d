#include "font_engine_freetype_internal.hpp"

#ifdef CHRONON3D_ENABLE_TEXT

namespace chronon3d {

namespace {
struct OutlineContourBuild {
    std::vector<std::vector<Vec2>> contours;
    std::vector<Vec2> current;
    Vec2 cursor{};
    bool has_cursor{false};
};

Vec2 outline_point(const FT_Vector* p) {
    constexpr float scale = 1.0f / 64.0f;
    return Vec2{static_cast<float>(p->x) * scale,
                -static_cast<float>(p->y) * scale};
}

void outline_append(OutlineContourBuild& b, Vec2 p) {
    if (!b.has_cursor || glm::length(p - b.cursor) > 1e-4f)
        b.current.push_back(p);
    b.cursor = p;
    b.has_cursor = true;
}

void outline_finish(OutlineContourBuild& b) {
    if (b.current.size() > 2) b.contours.push_back(std::move(b.current));
    b.current.clear();
    b.has_cursor = false;
}

int outline_move_to(const FT_Vector* to, void* user) {
    auto& b = *static_cast<OutlineContourBuild*>(user);
    outline_finish(b);
    outline_append(b, outline_point(to));
    return 0;
}

int outline_line_to(const FT_Vector* to, void* user) {
    outline_append(*static_cast<OutlineContourBuild*>(user), outline_point(to));
    return 0;
}

int outline_conic_to(const FT_Vector* control, const FT_Vector* to, void* user) {
    auto& b = *static_cast<OutlineContourBuild*>(user);
    const Vec2 p0 = b.cursor, p1 = outline_point(control), p2 = outline_point(to);
    const int steps = std::clamp(static_cast<int>(std::ceil(
        (glm::length(p1 - p0) + glm::length(p2 - p1)) / 2.0f)), 2, 32);
    for (int i = 1; i <= steps; ++i) {
        const float t = static_cast<float>(i) / steps, u = 1.0f - t;
        outline_append(b, u * u * p0 + 2.0f * u * t * p1 + t * t * p2);
    }
    return 0;
}

int outline_cubic_to(const FT_Vector* c1, const FT_Vector* c2,
                     const FT_Vector* to, void* user) {
    auto& b = *static_cast<OutlineContourBuild*>(user);
    const Vec2 p0 = b.cursor, p1 = outline_point(c1),
               p2 = outline_point(c2), p3 = outline_point(to);
    const int steps = std::clamp(static_cast<int>(std::ceil(
        (glm::length(p1 - p0) + glm::length(p2 - p1) +
         glm::length(p3 - p2)) / 2.0f)), 3, 48);
    for (int i = 1; i <= steps; ++i) {
        const float t = static_cast<float>(i) / steps, u = 1.0f - t;
        outline_append(b, u*u*u*p0 + 3.0f*u*u*t*p1 +
                          3.0f*u*t*t*p2 + t*t*t*p3);
    }
    return 0;
}
} // namespace

std::vector<std::vector<Vec2>> FontEngine::glyph_outline_contours(
    const FontSpec& spec, u32 glyph_id, float font_size,
    std::string_view variation_axes) const {
    if (!m_impl || !m_impl->ft_library || glyph_id == 0 ||
        !std::isfinite(font_size) || font_size <= 0.0f) return {};
    auto entry = m_impl->get_face_entry(spec);
    if (!entry || !entry->valid()) return {};
    std::lock_guard<std::mutex> lock(entry->ops_mutex);
    const std::string axes{variation_axes};
    if (!apply_variation_axes(*entry, m_impl->ft_library, axes) ||
        FT_Set_Pixel_Sizes(entry->ft_face, 0,
            static_cast<unsigned int>(std::ceil(font_size))) != 0 ||
        FT_Load_Glyph(entry->ft_face, glyph_id,
            FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING) != 0 ||
        entry->ft_face->glyph->format != FT_GLYPH_FORMAT_OUTLINE) return {};

    OutlineContourBuild build;
    FT_Outline_Funcs funcs{};
    funcs.move_to = outline_move_to;
    funcs.line_to = outline_line_to;
    funcs.conic_to = outline_conic_to;
    funcs.cubic_to = outline_cubic_to;
    if (FT_Outline_Decompose(&entry->ft_face->glyph->outline,
                             &funcs, &build) != 0) return {};
    outline_finish(build);
    return build.contours;
}

std::optional<GlyphRun> FontEngine::shape_text(
    std::string_view text,
    const FontSpec& spec,
    float font_size,
    const TextShaping& shaping
) const {
    if (!m_impl || !m_impl->ft_library || text.empty() || font_size <= 0.0f) {
        return std::nullopt;
    }

    const auto shaping_start = profiling::now();

    auto entry = m_impl->get_face_entry(spec);
    if (!entry || !entry->valid()) return std::nullopt;
    std::lock_guard<std::mutex> font_lock(entry->ops_mutex);

    if (!apply_variation_axes(*entry, m_impl->ft_library, shaping.variation_axes)) {
        return std::nullopt;
    }

    FT_Face face = entry->ft_face;
    const float scale = Impl::pixel_scale_26_6();

    FT_Error size_err = FT_Set_Pixel_Sizes(face, 0, static_cast<unsigned int>(std::ceil(font_size)));
    if (size_err != 0) return std::nullopt;
    hb_ft_font_changed(entry->hb_font);

    hb_buffer_t* buf = m_impl->acquire_hb_buffer();
    if (!buf) return std::nullopt;
    constexpr std::string_view kPreserveMarkPaint = "svg-preserve-mark-paint=1";
    const bool preserve_mark_paint =
        shaping.features.find(kPreserveMarkPaint) != std::string::npos;
    hb_buffer_set_unicode_funcs(buf, preserve_mark_paint
        ? mark_paint_unicode_funcs() : hb_unicode_funcs_get_default());
    // Preserve one cluster index per source character so post-shaping paint
    // ranges can color a combining mark independently without splitting the
    // positioned run. The shaping features and glyph positions remain driven
    // by HarfBuzz's normal shaping pipeline.
    hb_buffer_set_cluster_level(buf, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS);
    hb_buffer_add_utf8(buf, text.data(), static_cast<int>(text.size()), 0, static_cast<int>(text.size()));

    if (shaping.direction == TextDirection::RTL) {
        hb_buffer_set_direction(buf, HB_DIRECTION_RTL);
    } else if (shaping.direction == TextDirection::LTR) {
        hb_buffer_set_direction(buf, HB_DIRECTION_LTR);
    }

    if (shaping.script != 0) {
        hb_buffer_set_script(buf, static_cast<hb_script_t>(shaping.script));
    }

    if (!shaping.language.empty()) {
        hb_buffer_set_language(buf,
            hb_language_from_string(shaping.language.c_str(), -1));
    }

    hb_buffer_guess_segment_properties(buf);

    const auto hb_features = parse_opentype_features(shaping.features);
    hb_shape(entry->hb_font, buf,
             hb_features.data(),
             static_cast<unsigned int>(hb_features.size()));

    unsigned int glyph_count = 0;
    hb_glyph_info_t* glyph_infos = hb_buffer_get_glyph_infos(buf, &glyph_count);
    hb_glyph_position_t* glyph_positions = hb_buffer_get_glyph_positions(buf, &glyph_count);

    GlyphRun run;
    run.font_size = font_size;
    run.glyphs.reserve(glyph_count);

    float cursor_x = 0.0f;
    float cursor_y = 0.0f;
    const u32 pixel_size = static_cast<u32>(std::ceil(font_size));

    for (unsigned int i = 0; i < glyph_count; ++i) {
        GlyphPosition gp;
        gp.glyph_id = static_cast<u32>(glyph_infos[i].codepoint);
        gp.x_offset = static_cast<float>(glyph_positions[i].x_offset) * scale;
        gp.y_offset = static_cast<float>(glyph_positions[i].y_offset) * scale;
        gp.x = cursor_x + gp.x_offset;
        gp.y = cursor_y + gp.y_offset;
        gp.advance_x = static_cast<float>(glyph_positions[i].x_advance) * scale;
        gp.advance_y = static_cast<float>(glyph_positions[i].y_advance) * scale;
        gp.cluster = static_cast<u32>(glyph_infos[i].cluster);
        gp.is_cluster_start = (i == 0) ||
                              (glyph_infos[i].cluster != glyph_infos[i - 1].cluster);

        GlyphBBoxCacheKey key{entry->id, gp.glyph_id, pixel_size, shaping.variation_axes};
        auto cached = m_impl->glyph_bbox_cache.get(key);
        if (cached) {
            gp.bbox_x0 = cached->x0;
            gp.bbox_y0 = cached->y0;
            gp.bbox_x1 = cached->x1;
            gp.bbox_y1 = cached->y1;
        } else {
            FT_Error err = FT_Load_Glyph(face, gp.glyph_id, FT_LOAD_DEFAULT);
            if (err == 0) {
                FT_GlyphSlot slot = face->glyph;
                if (slot->format == FT_GLYPH_FORMAT_OUTLINE) {
                    FT_BBox outline_bbox;
                    FT_Outline_Get_BBox(&slot->outline, &outline_bbox);
                    gp.bbox_x0 = static_cast<float>(outline_bbox.xMin) * scale;
                    gp.bbox_y0 = static_cast<float>(outline_bbox.yMax) * scale;
                    gp.bbox_x1 = static_cast<float>(outline_bbox.xMax) * scale;
                    gp.bbox_y1 = static_cast<float>(outline_bbox.yMin) * scale;
                } else {
                    gp.bbox_x0 = static_cast<float>(slot->metrics.horiBearingX) * scale;
                    gp.bbox_y0 = static_cast<float>(slot->metrics.horiBearingY) * scale;
                    gp.bbox_x1 = gp.bbox_x0 + static_cast<float>(slot->metrics.width) * scale;
                    gp.bbox_y1 = gp.bbox_y0 - static_cast<float>(slot->metrics.height) * scale;
                }

                m_impl->glyph_bbox_cache.put(key, GlyphBBoxCacheEntry{
                    gp.bbox_x0, gp.bbox_y0, gp.bbox_x1, gp.bbox_y1
                }, /*weight=*/1);
            }
        }

        run.glyphs.push_back(gp);
        cursor_x += gp.advance_x;
        cursor_y += gp.advance_y;
    }

    m_impl->release_hb_buffer(buf);

    // RTL advances are negative in HarfBuzz's pen direction, while layout
    // boxes and measure_text expose an unsigned visual width.
    run.width = std::abs(cursor_x);
    run.ascent  = static_cast<float>(face->size->metrics.ascender)  * scale;
    run.descent = -static_cast<float>(face->size->metrics.descender) * scale;
    run.baseline = 0.0f;
    run.line_height = static_cast<float>(face->size->metrics.height) * scale;

    if (profiling::g_current_counters) {
        profiling::g_current_counters->text_shaping_calls.fetch_add(1, std::memory_order_relaxed);
        profiling::g_current_counters->text_shaping_wall_ms.fetch_add(
            static_cast<uint64_t>(std::llround(profiling::elapsed_ms(shaping_start))),
            std::memory_order_relaxed);
    }

    return run;
}


} // namespace chronon3d

#endif // CHRONON3D_ENABLE_TEXT
