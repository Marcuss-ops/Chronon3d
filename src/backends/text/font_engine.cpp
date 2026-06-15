#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/cache/lru_cache.hpp>
#include <chronon3d/backends/text/text_layout_engine.hpp>
#include <spdlog/spdlog.h>

#ifdef CHRONON3D_ENABLE_TEXT
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_OUTLINE_H

#include <hb.h>
#include <hb-ft.h>
#endif

#include <cmath>
#include <algorithm>
#include <set>
#include <shared_mutex>
#include <unordered_map>

namespace chronon3d {

// =============================================================================
// Full implementation with FreeType + HarfBuzz
// =============================================================================
#ifdef CHRONON3D_ENABLE_TEXT

struct FaceEntry {
    FT_Face ft_face{nullptr};
    hb_font_t* hb_font{nullptr};
    std::string resolved_path;
    int font_weight{400};
    bool has_kerning{false};

    bool valid() const { return ft_face != nullptr && hb_font != nullptr; }
};

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
    mutable std::shared_mutex face_cache_mutex;
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
            resolved = spec.font_path;
        }

        FT_Face face = nullptr;
        FT_Error err = FT_New_Face(ft_library, resolved.c_str(), 0, &face);
        if (err != 0) {
            spdlog::warn("FontEngine: failed to load font '{}' (resolved: '{}', error={})",
                         spec.font_path, resolved, err);
            return std::nullopt;
        }

        FT_Set_Pixel_Sizes(face, 0, 16);

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

    [[nodiscard]] static float pixel_scale_26_6() {
        return 1.0f / 64.0f;
    }
};

// ── FontEngine public API (full) ─────────────────────────────────────

FontEngine::FontEngine() : m_impl(std::make_unique<Impl>()) {}
FontEngine::~FontEngine() = default;
FontEngine::FontEngine(FontEngine&&) noexcept = default;
FontEngine& FontEngine::operator=(FontEngine&&) noexcept = default;

std::optional<GlyphRun> FontEngine::shape_text(
    std::string_view text,
    const FontSpec& spec,
    float font_size,
    const TextShaping& shaping
) const {
    if (!m_impl || !m_impl->ft_library || text.empty() || font_size <= 0.0f) {
        return std::nullopt;
    }

    std::unique_lock<std::shared_mutex> face_lock(m_impl->face_cache_mutex);
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
    const float scale = Impl::pixel_scale_26_6();

    FT_Error size_err = FT_Set_Pixel_Sizes(face, 0, static_cast<unsigned int>(std::ceil(font_size)));
    if (size_err != 0) return std::nullopt;
    hb_ft_font_changed(entry->hb_font);

    hb_buffer_t* buf = hb_buffer_create();
    if (!buf) return std::nullopt;
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
        gp.x_offset = static_cast<float>(glyph_positions[i].x_offset) * scale;
        gp.y_offset = static_cast<float>(glyph_positions[i].y_offset) * scale;
        gp.x = cursor_x + gp.x_offset;
        gp.y = cursor_y + gp.y_offset;
        gp.advance_x = static_cast<float>(glyph_positions[i].x_advance) * scale;
        gp.advance_y = static_cast<float>(glyph_positions[i].y_advance) * scale;
        gp.cluster = static_cast<u32>(glyph_infos[i].cluster);
        gp.is_cluster_start = (i == 0) ||
                              (glyph_infos[i].cluster != glyph_infos[i - 1].cluster);

        GlyphBBoxCacheKey key{face, gp.glyph_id, pixel_size};
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
                gp.bbox_x0 = static_cast<float>(slot->metrics.horiBearingX) * scale;
                gp.bbox_y0 = static_cast<float>(slot->metrics.horiBearingY) * scale;
                gp.bbox_x1 = gp.bbox_x0 + static_cast<float>(slot->metrics.width) * scale;
                gp.bbox_y1 = gp.bbox_y0 - static_cast<float>(slot->metrics.height) * scale;

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
    run.ascent  = static_cast<float>(face->size->metrics.ascender)  * scale;
    run.descent = -static_cast<float>(face->size->metrics.descender) * scale;
    run.baseline = 0.0f;
    run.line_height = static_cast<float>(face->size->metrics.height) * scale;

    return run;
}

float FontEngine::measure_text(std::string_view text, const FontSpec& spec, float font_size, const TextShaping& shaping) const {
    auto run = shape_text(text, spec, font_size, shaping);
    if (!run) return 0.0f;
    return run->width;
}

FontEngine::FontMetrics FontEngine::get_font_metrics(const FontSpec& spec, float font_size) const {
    FontMetrics metrics{};
    if (!m_impl || !m_impl->ft_library || font_size <= 0.0f) return metrics;

    std::unique_lock<std::shared_mutex> face_lock(m_impl->face_cache_mutex);
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

void FontEngine::clear_cache() {
    if (!m_impl) return;
    std::unique_lock<std::shared_mutex> lock(m_impl->face_cache_mutex);
    m_impl->clear_cache_unlocked();
}

size_t FontEngine::glyph_bbox_cache_size() const {
    if (!m_impl) return 0;
    return m_impl->glyph_bbox_cache.stats().current_size;
}

bool FontEngine::can_load(const FontSpec& spec) {
    if (!m_impl || !m_impl->ft_library) return false;
    {
        std::shared_lock<std::shared_mutex> shared_lock(m_impl->face_cache_mutex);
        auto it = m_impl->face_cache.find(spec);
        if (it != m_impl->face_cache.end()) {
            return it->second.valid();
        }
    }
    std::unique_lock<std::shared_mutex> unique_lock(m_impl->face_cache_mutex);
    auto it = m_impl->face_cache.find(spec);
    if (it != m_impl->face_cache.end()) {
        return it->second.valid();
    }
    auto entry = m_impl->load_face(spec);
    if (!entry) return false;
    auto [inserted_it, ok] = m_impl->face_cache.emplace(spec, std::move(*entry));
    return ok && inserted_it->second.valid();
}

// =============================================================================
// Stub implementation (no FreeType / HarfBuzz available)
// =============================================================================
#else

struct FontEngine::Impl {
    ~Impl() = default;
};

FontEngine::FontEngine() : m_impl(std::make_unique<Impl>()) {}
FontEngine::~FontEngine() = default;
FontEngine::FontEngine(FontEngine&&) noexcept = default;
FontEngine& FontEngine::operator=(FontEngine&&) noexcept = default;

std::optional<GlyphRun> FontEngine::shape_text(
    std::string_view, const FontSpec&, float, const TextShaping&
) const {
    return std::nullopt;
}

float FontEngine::measure_text(
    std::string_view, const FontSpec&, float, const TextShaping&
) const {
    return 0.0f;
}

FontEngine::FontMetrics FontEngine::get_font_metrics(
    const FontSpec&, float
) const {
    return FontMetrics{};
}

void FontEngine::clear_cache() {}

size_t FontEngine::glyph_bbox_cache_size() const { return 0; }

bool FontEngine::can_load(const FontSpec&) { return false; }

#endif // CHRONON3D_ENABLE_TEXT

// ── Global convenience (always available) ─────────────────────────────

std::optional<GlyphRun> shape_text(std::string_view text, const FontSpec& spec, float font_size, const TextShaping& shaping) {
    static FontEngine engine;
    return engine.shape_text(text, spec, font_size, shaping);
}

FontEngine& shared_font_engine() {
    static FontEngine engine;
    return engine;
}

void reset_shared_font_engine() {
    auto& engine = shared_font_engine();
    engine.clear_cache();
}

// ── PlacedGlyphRun resolver (no freetype/harfbuzz dependency) ────────

PlacedGlyphRun resolve_placed_glyph_run(
    const GlyphRun& hb_run,
    float tracking,
    std::string_view source_text
) {
    PlacedGlyphRun result;
    result.ascent = hb_run.ascent;
    result.descent = hb_run.descent;
    result.baseline = hb_run.baseline;
    result.font_size = hb_run.font_size;

    if (hb_run.glyphs.empty()) return result;

    float pen_x = 0.0f;
    float pen_y = 0.0f;
    result.glyphs.reserve(hb_run.glyphs.size());

    for (size_t i = 0; i < hb_run.glyphs.size(); ++i) {
        const auto& g = hb_run.glyphs[i];
        PlacedGlyph pg;
        pg.glyph_id = g.glyph_id;
        pg.x_offset = g.x_offset;
        pg.y_offset = g.y_offset;
        pg.is_cluster_start = g.is_cluster_start;
        pg.cluster = g.cluster;

        pg.x = pen_x + g.x_offset;
        pg.y = pen_y + g.y_offset;

        pg.raw_advance_x = g.advance_x;
        pg.advance_x = g.advance_x;
        pg.advance_y = g.advance_y;

        if (tracking != 0.0f && i + 1 < hb_run.glyphs.size()) {
            if (hb_run.glyphs[i + 1].is_cluster_start) {
                pg.advance_x += tracking;
            }
        }

        result.glyphs.push_back(pg);
        pen_x += pg.advance_x;
        pen_y += g.advance_y;
    }

    result.total_width = pen_x;
    result.total_height = hb_run.ascent + hb_run.descent;

    if (!source_text.empty()) {
        std::set<u32> cluster_set;
        for (const auto& g : hb_run.glyphs) {
            cluster_set.insert(g.cluster);
        }
        cluster_set.insert(static_cast<u32>(source_text.size()));

        std::vector<u32> sorted_clusters(cluster_set.begin(), cluster_set.end());
        std::unordered_map<u32, std::pair<size_t, size_t>> cluster_to_range;
        for (size_t k = 0; k + 1 < sorted_clusters.size(); ++k) {
            cluster_to_range[sorted_clusters[k]] = {
                static_cast<size_t>(sorted_clusters[k]),
                static_cast<size_t>(sorted_clusters[k + 1])
            };
        }

        result.clusters.reserve(sorted_clusters.size() - 1);
        for (size_t k = 0; k + 1 < sorted_clusters.size(); ++k) {
            const u32 c = sorted_clusters[k];
            auto it = cluster_to_range.find(c);
            if (it == cluster_to_range.end()) continue;

            const auto& [start_byte, end_byte] = it->second;
            if (end_byte <= start_byte || start_byte >= source_text.size()) continue;

            PlacedGlyphRun::Cluster cl;
            cl.byte_offset = start_byte;
            cl.byte_len = end_byte - start_byte;
            for (size_t gi = 0; gi < result.glyphs.size(); ++gi) {
                if (result.glyphs[gi].cluster == c) {
                    if (cl.start_glyph == 0 && cl.end_glyph == 0) {
                        cl.start_glyph = gi;
                    }
                    cl.end_glyph = gi + 1;
                    cl.advance += result.glyphs[gi].advance_x;
                    cl.raw_advance += result.glyphs[gi].raw_advance_x;
                }
            }

            result.clusters.push_back(cl);
        }

        // ── Third pass: populate per-glyph source fields ───────────
        // Copy byte offset/len from cluster info back to individual
        // PlacedGlyph entries so every glyph exposes its source range.
        for (const auto& cl : result.clusters) {
            for (size_t gi = cl.start_glyph; gi < cl.end_glyph; ++gi) {
                if (gi < result.glyphs.size()) {
                    result.glyphs[gi].byte_offset = cl.byte_offset;
                    result.glyphs[gi].byte_len = cl.byte_len;
                }
            }
        }
    }

    return result;
}

} // namespace chronon3d
