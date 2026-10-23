#include "font_engine_freetype_internal.hpp"

#ifdef CHRONON3D_ENABLE_TEXT

#include <algorithm>
#include <cmath>
#include <cctype>
#include <stdexcept>

namespace chronon3d {

FontEngine::FontEngine(const chronon3d::assets::AssetResolver& resolver,
                       TextLayoutCache* layout_cache)
    : m_impl(std::make_unique<Impl>(&resolver)),
      m_compat_layout_cache(layout_cache ? nullptr : std::make_unique<TextLayoutCache>()),
      m_text_layout_cache(layout_cache ? layout_cache : m_compat_layout_cache.get()) {}

FontEngine::~FontEngine() = default;
FontEngine::FontEngine(FontEngine&&) noexcept = default;
FontEngine& FontEngine::operator=(FontEngine&&) noexcept = default;

float FontEngine::measure_text(std::string_view text, const FontSpec& spec, float font_size, const TextShaping& shaping) const {
    auto run = shape_text(text, spec, font_size, shaping);
    if (!run) return 0.0f;
    return run->width;
}

FontEngine::FontMetrics FontEngine::get_font_metrics(const FontSpec& spec, float font_size) const {
    FontMetrics metrics{};
    if (!m_impl || !m_impl->ft_library || font_size <= 0.0f) return metrics;

    auto entry = m_impl->get_face_entry(spec);
    if (!entry || !entry->valid()) return metrics;
    std::lock_guard<std::mutex> font_lock(entry->ops_mutex);

    FT_Face face = entry->ft_face;
    const float scale = Impl::pixel_scale_26_6();

    FT_Error size_err = FT_Set_Pixel_Sizes(face, 0, static_cast<unsigned int>(std::ceil(font_size)));
    if (size_err != 0) return metrics;

    metrics.ascent  = static_cast<float>(face->size->metrics.ascender)  * scale;
    metrics.descent = -static_cast<float>(face->size->metrics.descender) * scale;
    metrics.line_height = static_cast<float>(face->size->metrics.height) * scale;

    FT_Load_Char(face, 'x', FT_LOAD_DEFAULT);
    if (face->glyph) {
        metrics.x_height = static_cast<float>(face->glyph->metrics.horiBearingY) * scale;
    }

    FT_Load_Char(face, 'H', FT_LOAD_DEFAULT);
    if (face->glyph) {
        metrics.cap_height = static_cast<float>(face->glyph->metrics.horiBearingY) * scale;
    }

    metrics.max_advance = static_cast<float>(face->size->metrics.max_advance) * scale;
    return metrics;
}

std::optional<float> FontEngine::get_open_type_baseline_offset(
    const FontSpec& spec,
    float font_size,
    std::string_view baseline,
    std::string_view script,
    TextDirection direction
) const {
    if (!m_impl || !m_impl->ft_library || font_size <= 0.0f ||
        script.size() != 4 || baseline.empty()) return std::nullopt;

    const hb_script_t hb_script = hb_script_from_string(script.data(),
        static_cast<int>(script.size()));
    hb_ot_layout_baseline_tag_t tag;
    if (baseline == "auto" || baseline == "use-script") {
        tag = hb_ot_layout_get_horizontal_baseline_tag_for_script(hb_script);
        if (tag == _HB_OT_LAYOUT_BASELINE_TAG_MAX_VALUE)
            tag = HB_OT_LAYOUT_BASELINE_TAG_ROMAN;
    } else if (baseline == "alphabetic") {
        tag = HB_OT_LAYOUT_BASELINE_TAG_ROMAN;
    } else if (baseline == "central")
        tag = HB_OT_LAYOUT_BASELINE_TAG_IDEO_EMBOX_CENTRAL;
    else if (baseline == "hanging")
        tag = HB_OT_LAYOUT_BASELINE_TAG_HANGING;
    else if (baseline == "ideographic")
        tag = HB_OT_LAYOUT_BASELINE_TAG_IDEO_EMBOX_BOTTOM_OR_LEFT;
    else if (baseline == "mathematical")
        tag = HB_OT_LAYOUT_BASELINE_TAG_MATH;
    else if (baseline == "text-before-edge")
        tag = HB_OT_LAYOUT_BASELINE_TAG_IDEO_EMBOX_TOP_OR_RIGHT;
    else if (baseline == "text-after-edge")
        tag = HB_OT_LAYOUT_BASELINE_TAG_IDEO_EMBOX_BOTTOM_OR_LEFT;
    else
        return std::nullopt;

    auto entry = m_impl->get_face_entry(spec);
    if (!entry || !entry->valid()) return std::nullopt;
    std::lock_guard<std::mutex> font_lock(entry->ops_mutex);
    FT_Face face = entry->ft_face;
    if (FT_Set_Pixel_Sizes(face, 0, static_cast<unsigned int>(std::ceil(font_size))) != 0)
        return std::nullopt;
    hb_ft_font_changed(entry->hb_font);

    const hb_direction_t hb_direction = direction == TextDirection::RTL
        ? HB_DIRECTION_RTL : HB_DIRECTION_LTR;
    hb_position_t roman = 0;
    hb_position_t target = 0;
    if (!hb_ot_layout_get_baseline2(entry->hb_font,
            HB_OT_LAYOUT_BASELINE_TAG_ROMAN, hb_direction, hb_script,
            HB_LANGUAGE_INVALID, &roman) ||
        !hb_ot_layout_get_baseline2(entry->hb_font,
            tag, hb_direction, hb_script, HB_LANGUAGE_INVALID, &target))
        return std::nullopt;

    return static_cast<float>(target - roman) * Impl::pixel_scale_26_6();
}

void FontEngine::clear_cache() {
    if (!m_impl) return;
    m_impl->clear_cache_unlocked();
}

size_t FontEngine::glyph_bbox_cache_size() const {
    if (!m_impl) return 0;
    return m_impl->glyph_bbox_cache.stats().current_size;
}

chronon3d::content::text::TypewriterLayoutCache& FontEngine::typewriter_layout_cache() {
    if (!m_impl || !m_impl->typewriter_layout_cache) {
        throw std::runtime_error("FontEngine::typewriter_layout_cache() called on an invalid engine");
    }
    return *m_impl->typewriter_layout_cache;
}

TextLayoutCache* FontEngine::text_layout_cache() noexcept {
    return m_text_layout_cache;
}

bool FontEngine::can_load(const FontSpec& spec) {
    if (!m_impl || !m_impl->ft_library) return false;
    auto entry = m_impl->get_face_entry(spec);
    return entry != nullptr && entry->valid();
}

namespace text::font_engine_internal {

bool has_glyph_for_codepoint(FontEngine& engine, const FontSpec& spec, char32_t codepoint) {
#ifdef CHRONON3D_ENABLE_TEXT
    if (!engine.m_impl) return false;

    if (is_invisible_codepoint(codepoint)) {
        return true;
    }

    auto entry = engine.m_impl->get_face_entry(spec);
    if (!entry) return false;
    std::lock_guard<std::mutex> font_lock(entry->ops_mutex);

    FT_UInt glyph_index = FT_Get_Char_Index(entry->ft_face, static_cast<FT_ULong>(codepoint));
    if (glyph_index == 0 || glyph_index == entry->notdef_glyph_id) {
        return false;
    }

    FT_Error err = FT_Load_Glyph(entry->ft_face, glyph_index, FT_LOAD_NO_SCALE);
    if (err != 0) {
        return false;
    }
    const FT_GlyphSlot slot = entry->ft_face->glyph;
    if (slot->format == FT_GLYPH_FORMAT_OUTLINE) {
        return slot->outline.n_points > 0;
    }
    if (slot->format == FT_GLYPH_FORMAT_BITMAP) {
        return slot->bitmap.width > 0 && slot->bitmap.rows > 0;
    }
    return true;
#else
    (void)engine; (void)spec; (void)codepoint;
    return false;
#endif
}

bool inspect_font(
    FontEngine&     engine,
    const FontSpec& spec,
    std::string&    out_family,
    std::string&    out_style,
    int&            out_weight
) {
#ifdef CHRONON3D_ENABLE_TEXT
    if (!engine.m_impl) return false;
    auto entry = engine.m_impl->get_face_entry(spec);
    if (!entry) return false;

    out_family = entry->family_name;
    out_style  = entry->style_name;
    out_weight = entry->font_weight;

    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };
    if (out_weight == 400 && lower(out_style).find("bold") != std::string::npos) {
        out_weight = 700;
    }

    return true;
#else
    (void)engine; (void)spec;
    (void)out_family; (void)out_style; (void)out_weight;
    return false;
#endif
}

} // namespace chronon3d::text::font_engine_internal


} // namespace chronon3d

#endif // CHRONON3D_ENABLE_TEXT
