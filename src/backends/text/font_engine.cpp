#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/cache/lru_cache.hpp>
#include <spdlog/spdlog.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_OUTLINE_H

#include <hb.h>
#include <hb-ft.h>

#include <cmath>
#include <algorithm>

namespace chronon3d {

// ── Internal face cache entry ─────────────────────────────────────────

struct FaceEntry {
    FT_Face ft_face{nullptr};
    hb_font_t* hb_font{nullptr};
    std::string resolved_path;
    int font_weight{400};
    bool has_kerning{false};

    bool valid() const { return ft_face != nullptr && hb_font != nullptr; }
};

// ── Glyph bbox cache ──────────────────────────────────────────────────
// Keyed by (face pointer, glyph_id, pixel_size) for fast bbox lookups.

struct GlyphBBoxCacheKey {
    FT_Face face{nullptr};
    u32     glyph_id{0};
    u32     pixel_size{0};

    bool operator==(const GlyphBBoxCacheKey& o) const noexcept {
        return face == o.face && glyph_id == o.glyph_id && pixel_size == o.pixel_size;
    }
};

struct GlyphBBoxCacheEntry {
    float x0{0.0f};
    float y0{0.0f};
    float x1{0.0f};
    float y1{0.0f};
};

} // namespace chronon3d

namespace std {
template<> struct hash<chronon3d::GlyphBBoxCacheKey> {
    [[nodiscard]] size_t operator()(const chronon3d::GlyphBBoxCacheKey& k) const noexcept {
        size_t h = 0;
        h ^= std::hash<void*>{}(k.face) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<chronon3d::u32>{}(k.glyph_id) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<chronon3d::u32>{}(k.pixel_size) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};
} // namespace std

namespace chronon3d {

struct FontEngine::Impl {
    FT_Library ft_library{nullptr};
    mutable std::unordered_map<FontSpec, FaceEntry, std::hash<FontSpec>> face_cache;
    mutable std::mutex cache_mutex;
    mutable cache::LruCache<GlyphBBoxCacheKey, GlyphBBoxCacheEntry, std::hash<GlyphBBoxCacheKey>> glyph_bbox_cache{8192, 2};

    Impl() {
        FT_Error err = FT_Init_FreeType(&ft_library);
        if (err != 0) {
            spdlog::error("FontEngine: FT_Init_FreeType failed (error={})", err);
            ft_library = nullptr;
        }
    }

    ~Impl() {
        clear_cache_unlocked();
        if (ft_library) {
            FT_Done_FreeType(ft_library);
        }
    }

    void clear_cache_unlocked() {
        for (auto& [spec, entry] : face_cache) {
            if (entry.hb_font) {
                hb_font_destroy(entry.hb_font);
            }
            if (entry.ft_face) {
                FT_Done_Face(entry.ft_face);
            }
        }
        face_cache.clear();
        glyph_bbox_cache.clear();
    }

    std::optional<FaceEntry> load_face(const FontSpec& spec) {
        if (!ft_library) return std::nullopt;

        std::string resolved = AssetRegistry::resolve(spec.font_path);
        if (resolved.empty()) {
            resolved = spec.font_path; // try raw path
        }

        FT_Face face = nullptr;
        FT_Error err = FT_New_Face(ft_library, resolved.c_str(), 0, &face);
        if (err != 0) {
            spdlog::warn("FontEngine: failed to load font '{}' (resolved: '{}', error={})",
                         spec.font_path, resolved, err);
            return std::nullopt;
        }

        // Set a nominal size so hb_ft_font_create gets a face with valid metrics.
        // Per-call sizing in shape_text / get_font_metrics overrides this.
        FT_Set_Pixel_Sizes(face, 0, 16);

        // Create HarfBuzz font from FreeType face
        hb_font_t* hb_font = hb_ft_font_create(face, nullptr);
        if (!hb_font) {
            FT_Done_Face(face);
            return std::nullopt;
        }

        FaceEntry entry;
        entry.ft_face = face;
        entry.hb_font = hb_font;
        entry.resolved_path = std::move(resolved);
        entry.font_weight = spec.font_weight;
        entry.has_kerning = FT_HAS_KERNING(face);

        return entry;
    }

        // Scale factor for HarfBuzz glyph positions (font units -> pixels).
    [[nodiscard]] static float font_unit_scale(FT_Face face, float font_size) {
        if (!face || face->units_per_EM == 0) return font_size / 64.0f;
        return font_size / static_cast<float>(face->units_per_EM);
    }

    // FT size metrics and glyph metrics are in 26.6 fixed-point after FT_Set_Pixel_Sizes.
    [[nodiscard]] static float ft_pixel_scale() {
        return 1.0f / 64.0f;
    }
};

// ── FontEngine public API ─────────────────────────────────────────────

FontEngine::FontEngine() : m_impl(std::make_unique<Impl>()) {}
FontEngine::~FontEngine() = default;

FontEngine::FontEngine(FontEngine&&) noexcept = default;
FontEngine& FontEngine::operator=(FontEngine&&) noexcept = default;

std::optional<GlyphRun> FontEngine::shape_text(
    std::string_view text,
    const FontSpec& spec,
    float font_size
) const {
    if (!m_impl || !m_impl->ft_library || text.empty() || font_size <= 0.0f) {
        return std::nullopt;
    }

    // Lock the cache for the entire shaping operation to ensure thread safety.
    std::lock_guard<std::mutex> cache_lock(m_impl->cache_mutex);
    auto it = m_impl->face_cache.find(spec);
    if (it == m_impl->face_cache.end()) {
        auto entry = m_impl->load_face(spec);
        if (!entry) return std::nullopt;
        auto [inserted_it, ok] = m_impl->face_cache.emplace(spec, std::move(*entry));
        if (!ok) return std::nullopt;
        it = inserted_it;
    }
    FaceEntry* entry = &it->second;
    if (!entry->valid()) return std::nullopt;

    FT_Face face = entry->ft_face;
    const float hb_scale = Impl::font_unit_scale(face, font_size);
    const float ft_scale = Impl::ft_pixel_scale();

    // Re-set pixel size in case font_size differs from cache creation size
    FT_Error size_err = FT_Set_Pixel_Sizes(face, 0, static_cast<unsigned int>(std::ceil(font_size)));
    if (size_err != 0) return std::nullopt;

    // Create HarfBuzz buffer and shape
    hb_buffer_t* buf = hb_buffer_create();
    if (!buf) return std::nullopt;
    hb_buffer_add_utf8(buf, text.data(), static_cast<int>(text.size()), 0, static_cast<int>(text.size()));
    hb_buffer_set_direction(buf, HB_DIRECTION_LTR);
    // TODO: expose script/language as parameters for non-Latin text support
    hb_buffer_set_script(buf, HB_SCRIPT_LATIN);
    hb_buffer_set_language(buf, hb_language_from_string("en", -1));

    hb_shape(entry->hb_font, buf, nullptr, 0);

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
        gp.x = cursor_x + static_cast<float>(glyph_positions[i].x_offset) * hb_scale;
        gp.y = cursor_y + static_cast<float>(glyph_positions[i].y_offset) * hb_scale;
        gp.advance_x = static_cast<float>(glyph_positions[i].x_advance) * hb_scale;
        gp.advance_y = static_cast<float>(glyph_positions[i].y_advance) * hb_scale;
        gp.cluster = static_cast<u32>(glyph_infos[i].cluster);
        gp.is_cluster_start = (glyph_infos[i].cluster == 0) ||
                              (i > 0 && glyph_infos[i].cluster != glyph_infos[i - 1].cluster);

        // Look up glyph bbox in cache first
        GlyphBBoxCacheKey key{face, gp.glyph_id, pixel_size};
        auto cached = m_impl->glyph_bbox_cache.get(key);
        if (cached) {
            gp.bbox_x0 = cached->x0;
            gp.bbox_y0 = cached->y0;
            gp.bbox_x1 = cached->x1;
            gp.bbox_y1 = cached->y1;
        } else {
            // Load glyph to get bounding box (FT glyph metrics are in 26.6 after FT_Set_Pixel_Sizes)
            FT_Error err = FT_Load_Glyph(face, gp.glyph_id, FT_LOAD_DEFAULT);
            if (err == 0) {
                FT_GlyphSlot slot = face->glyph;
                gp.bbox_x0 = static_cast<float>(slot->metrics.horiBearingX) * ft_scale;
                gp.bbox_y0 = static_cast<float>(slot->metrics.horiBearingY) * ft_scale;
                gp.bbox_x1 = gp.bbox_x0 + static_cast<float>(slot->metrics.width) * ft_scale;
                gp.bbox_y1 = gp.bbox_y0 - static_cast<float>(slot->metrics.height) * ft_scale;

                m_impl->glyph_bbox_cache.put(key, GlyphBBoxCacheEntry{
                    gp.bbox_x0, gp.bbox_y0, gp.bbox_x1, gp.bbox_y1
                }, /*weight=*/1);
            }
        }

        run.glyphs.push_back(gp);
        cursor_x += gp.advance_x;
        cursor_y += gp.advance_y;
    }

    hb_buffer_destroy(buf);

    run.width = cursor_x;
    // face->size->metrics are in 26.6 after FT_Set_Pixel_Sizes
    run.ascent  = static_cast<float>(face->size->metrics.ascender)  * ft_scale;
    run.descent = -static_cast<float>(face->size->metrics.descender) * ft_scale;
    run.baseline = 0.0f;
    run.line_height = static_cast<float>(face->size->metrics.height) * ft_scale;

    return run;
}

float FontEngine::measure_text(std::string_view text, const FontSpec& spec, float font_size) const {
    auto run = shape_text(text, spec, font_size);
    if (!run) return 0.0f;
    return run->width;
}

FontEngine::FontMetrics FontEngine::get_font_metrics(const FontSpec& spec, float font_size) const {
    FontMetrics metrics{};
    if (!m_impl || !m_impl->ft_library || font_size <= 0.0f) return metrics;

    // Lock for thread safety
    std::lock_guard<std::mutex> cache_lock(m_impl->cache_mutex);
    auto it = m_impl->face_cache.find(spec);
    if (it == m_impl->face_cache.end()) {
        auto entry = m_impl->load_face(spec);
        if (!entry) return metrics;
        auto [inserted_it, ok] = m_impl->face_cache.emplace(spec, std::move(*entry));
        if (!ok) return metrics;
        it = inserted_it;
    }
    FaceEntry* entry = &it->second;
    if (!entry->valid()) return metrics;

    FT_Face face = entry->ft_face;
    const float ft_scale = Impl::ft_pixel_scale();

    FT_Error size_err = FT_Set_Pixel_Sizes(face, 0, static_cast<unsigned int>(std::ceil(font_size)));
    if (size_err != 0) return metrics;

    // face->size->metrics are in 26.6 after FT_Set_Pixel_Sizes
    metrics.ascent  = static_cast<float>(face->size->metrics.ascender)  * ft_scale;
    metrics.descent = -static_cast<float>(face->size->metrics.descender) * ft_scale;
    metrics.line_height = static_cast<float>(face->size->metrics.height) * ft_scale;

    // x-height from 'x' glyph, cap-height from 'H' glyph (glyph metrics in 26.6)
    FT_Load_Char(face, 'x', FT_LOAD_DEFAULT);
    if (face->glyph) {
        metrics.x_height = static_cast<float>(face->glyph->metrics.horiBearingY) * ft_scale;
    }

    FT_Load_Char(face, 'H', FT_LOAD_DEFAULT);
    if (face->glyph) {
        metrics.cap_height = static_cast<float>(face->glyph->metrics.horiBearingY) * ft_scale;
    }

    metrics.max_advance = static_cast<float>(face->size->metrics.max_advance) * ft_scale;
    return metrics;
}

void FontEngine::clear_cache() {
    if (!m_impl) return;
    std::lock_guard<std::mutex> lock(m_impl->cache_mutex);
    m_impl->clear_cache_unlocked();
}

size_t FontEngine::glyph_bbox_cache_size() const {
    if (!m_impl) return 0;
    std::lock_guard<std::mutex> lock(m_impl->cache_mutex);
    return m_impl->glyph_bbox_cache.stats().current_size;
}

bool FontEngine::can_load(const FontSpec& spec) {
    if (!m_impl || !m_impl->ft_library) return false;
    std::lock_guard<std::mutex> cache_lock(m_impl->cache_mutex);
    auto it = m_impl->face_cache.find(spec);
    if (it != m_impl->face_cache.end()) {
        return it->second.valid();
    }
    auto entry = m_impl->load_face(spec);
    if (!entry) return false;
    auto [inserted_it, ok] = m_impl->face_cache.emplace(spec, std::move(*entry));
    return ok && inserted_it->second.valid();
}

// ── Global convenience ──────────────────────────────────────────────

std::optional<GlyphRun> shape_text(std::string_view text, const FontSpec& spec, float font_size) {
    static FontEngine engine;
    return engine.shape_text(text, spec, font_size);
}

FontEngine& shared_font_engine() {
    static FontEngine engine;
    return engine;
}

} // namespace chronon3d
