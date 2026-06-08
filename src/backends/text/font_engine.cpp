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
#include <bit>           // std::bit_cast for metrics cache key
#include <shared_mutex>

namespace chronon3d {

// ── Internal face cache entry ─────────────────────────────────────────

struct FaceEntry {
    FT_Face ft_face{nullptr};
    hb_font_t* hb_font{nullptr};
    std::string resolved_path;
    int font_weight{400};
    bool has_kerning{false};
    unsigned int last_pixel_size{0};  // OPT: cache last FT_Set_Pixel_Sizes

    bool valid() const { return ft_face != nullptr && hb_font != nullptr; }
};

// ── Glyph bbox cache ──────────────────────────────────────────────────

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

// ── Metrics cache key ─────────────────────────────────────────────────
// Hash-based key: combine FontSpec hash + font_size bits into uint64_t

struct MetricsCacheKey {
    u64 spec_hash{0};
    u32 font_size_bits{0};

    bool operator==(const MetricsCacheKey& o) const noexcept {
        return spec_hash == o.spec_hash && font_size_bits == o.font_size_bits;
    }
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

template<> struct hash<chronon3d::MetricsCacheKey> {
    [[nodiscard]] size_t operator()(const chronon3d::MetricsCacheKey& k) const noexcept {
        size_t h = 0;
        h ^= std::hash<chronon3d::u64>{}(k.spec_hash) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<chronon3d::u32>{}(k.font_size_bits) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};
} // namespace std

namespace chronon3d {

struct FontEngine::Impl {
    FT_Library ft_library{nullptr};
    mutable std::unordered_map<FontSpec, FaceEntry, std::hash<FontSpec>> face_cache;
    mutable std::shared_mutex face_cache_mutex;
    mutable std::mutex        glyph_bbox_mutex;
    mutable cache::LruCache<GlyphBBoxCacheKey, GlyphBBoxCacheEntry, std::hash<GlyphBBoxCacheKey>> glyph_bbox_cache{8192, 2};

    // OPT: metrics cache using hash-based key (avoids nested type issues)
    mutable std::shared_mutex metrics_cache_mutex;
    mutable std::unordered_map<MetricsCacheKey, FontMetrics, std::hash<MetricsCacheKey>> metrics_cache;

    // OPT: pool of reusable hb_buffer_t to avoid create/destroy per shape_text call
    static constexpr size_t kBufferPoolSize = 4;
    mutable std::mutex buffer_pool_mutex;
    mutable std::vector<hb_buffer_t*> buffer_pool;

    hb_buffer_t* acquire_buffer() const {
        std::lock_guard<std::mutex> lock(buffer_pool_mutex);
        if (!buffer_pool.empty()) {
            hb_buffer_t* buf = buffer_pool.back();
            buffer_pool.pop_back();
            hb_buffer_reset(buf);
            return buf;
        }
        return hb_buffer_create();
    }

    void release_buffer(hb_buffer_t* buf) const {
        if (!buf) return;
        std::lock_guard<std::mutex> lock(buffer_pool_mutex);
        if (buffer_pool.size() < kBufferPoolSize) {
            hb_buffer_clear_contents(buf);
            buffer_pool.push_back(buf);
        } else {
            hb_buffer_destroy(buf);
        }
    }

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
            if (entry.hb_font) hb_font_destroy(entry.hb_font);
            if (entry.ft_face) FT_Done_Face(entry.ft_face);
        }
        face_cache.clear();
        glyph_bbox_cache.clear();
        metrics_cache.clear();
        for (hb_buffer_t* buf : buffer_pool) hb_buffer_destroy(buf);
        buffer_pool.clear();
    }

    std::optional<FaceEntry> load_face(const FontSpec& spec) {
        if (!ft_library) return std::nullopt;

        std::string resolved = AssetRegistry::resolve(spec.font_path);
        if (resolved.empty()) resolved = spec.font_path;

        FT_Face face = nullptr;
        FT_Error err = FT_New_Face(ft_library, resolved.c_str(), 0, &face);
        if (err != 0) {
            spdlog::warn("FontEngine: failed to load font '{}' (resolved: '{}', error={})",
                         spec.font_path, resolved, err);
            return std::nullopt;
        }

        FT_Set_Pixel_Sizes(face, 0, 16);
        hb_font_t* hb_font = hb_ft_font_create(face, nullptr);
        if (!hb_font) { FT_Done_Face(face); return std::nullopt; }

        FaceEntry entry;
        entry.ft_face = face;
        entry.hb_font = hb_font;
        entry.resolved_path = std::move(resolved);
        entry.font_weight = spec.font_weight;
        entry.has_kerning = FT_HAS_KERNING(face);
        return entry;
    }

    [[nodiscard]] static float font_unit_scale(FT_Face face, float font_size) {
        if (!face || face->units_per_EM == 0) return font_size / 64.0f;
        return font_size / static_cast<float>(face->units_per_EM);
    }

    [[nodiscard]] static float ft_pixel_scale() { return 1.0f / 64.0f; }

    // OPT: face resolution helper that avoids redundant FT_Set_Pixel_Sizes.
    // Must be called with face_cache_mutex held (shared or unique).
    // Returns face_lock that keeps the shared/unique lock alive.
    struct FaceResult {
        FaceEntry* entry{nullptr};
        FT_Face    face{nullptr};
        float      hb_scale{0.0f};
        float      ft_scale{0.0f};
        bool       ok{false};
    };

    FaceResult resolve_face(const FontSpec& spec, float font_size) const {
        FaceResult r;
        if (!ft_library || font_size <= 0.0f) return r;

        auto it = face_cache.find(spec);
        if (it == face_cache.end()) {
            // Need to load — but we can't without a unique_lock
            // Caller must handle this case
            return r;
        }

        FaceEntry* entry = const_cast<FaceEntry*>(&it->second);
        if (!entry->valid()) return r;

        unsigned int req_size = static_cast<unsigned int>(std::ceil(font_size));
        if (entry->last_pixel_size != req_size) {
            FT_Error err = FT_Set_Pixel_Sizes(entry->ft_face, 0, req_size);
            if (err != 0) return r;
            entry->last_pixel_size = req_size;
        }

        r.entry = entry;
        r.face = entry->ft_face;
        r.hb_scale = font_unit_scale(r.face, font_size);
        r.ft_scale = ft_pixel_scale();
        r.ok = true;
        return r;
    }

    // Thread-safe face lookup: load if needed, set pixel sizes, return ready face
    FaceResult acquire_face(const FontSpec& spec, float font_size) const {
        // Fast path: try shared lock first
        {
            std::shared_lock lock(face_cache_mutex);
            auto it = face_cache.find(spec);
            if (it != face_cache.end()) {
                FaceResult r = resolve_face(spec, font_size);
                if (r.ok) return r;
            }
        }

        // Slow path: need unique lock to load face
        std::unique_lock wlock(face_cache_mutex);
        // Double-check
        auto it = face_cache.find(spec);
        if (it == face_cache.end()) {
            auto entry = const_cast<Impl*>(this)->load_face(spec);
            if (!entry) return FaceResult{};
            auto [inserted, ok] = const_cast<Impl*>(this)->face_cache.emplace(spec, std::move(*entry));
            if (!ok) return FaceResult{};
            it = inserted;
        }

        FaceEntry* entry = const_cast<FaceEntry*>(&it->second);
        unsigned int req_size = static_cast<unsigned int>(std::ceil(font_size));
        if (entry->last_pixel_size != req_size) {
            FT_Error err = FT_Set_Pixel_Sizes(entry->ft_face, 0, req_size);
            if (err != 0) return FaceResult{};
            entry->last_pixel_size = req_size;
        }

        FaceResult r;
        r.entry = entry;
        r.face = entry->ft_face;
        r.hb_scale = font_unit_scale(r.face, font_size);
        r.ft_scale = ft_pixel_scale();
        r.ok = true;
        return r;
    }
};

// ── FontEngine public API ─────────────────────────────────────────────

FontEngine::FontEngine() : m_impl(std::make_unique<Impl>()) {}
FontEngine::~FontEngine() = default;
FontEngine::FontEngine(FontEngine&&) noexcept = default;
FontEngine& FontEngine::operator=(FontEngine&&) noexcept = default;

// ── Helper: build a MetricsCacheKey from FontSpec + font_size ─────────
static MetricsCacheKey make_metrics_key(const FontSpec& spec, float font_size) {
    std::hash<FontSpec> h;
    MetricsCacheKey k;
    k.spec_hash = h(spec);
    k.font_size_bits = static_cast<u32>(std::bit_cast<u32>(font_size));
    return k;
}

// ── shape_text ────────────────────────────────────────────────────────

std::optional<GlyphRun> FontEngine::shape_text(
    std::string_view text,
    const FontSpec& spec,
    float font_size
) const {
    if (!m_impl || !m_impl->ft_library || text.empty() || font_size <= 0.0f) {
        return std::nullopt;
    }

    Impl::FaceResult fr = m_impl->acquire_face(spec, font_size);
    if (!fr.ok) return std::nullopt;

    hb_buffer_t* buf = m_impl->acquire_buffer();
    if (!buf) return std::nullopt;
    hb_buffer_add_utf8(buf, text.data(), static_cast<int>(text.size()), 0, static_cast<int>(text.size()));
    hb_buffer_set_direction(buf, HB_DIRECTION_LTR);
    hb_buffer_set_script(buf, HB_SCRIPT_LATIN);
    hb_buffer_set_language(buf, hb_language_from_string("en", -1));

    hb_shape(fr.entry->hb_font, buf, nullptr, 0);

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
        gp.x = cursor_x + static_cast<float>(glyph_positions[i].x_offset) * fr.hb_scale;
        gp.y = cursor_y + static_cast<float>(glyph_positions[i].y_offset) * fr.hb_scale;
        gp.advance_x = static_cast<float>(glyph_positions[i].x_advance) * fr.hb_scale;
        gp.advance_y = static_cast<float>(glyph_positions[i].y_advance) * fr.hb_scale;
        gp.cluster = static_cast<u32>(glyph_infos[i].cluster);
        gp.is_cluster_start = (glyph_infos[i].cluster == 0) ||
                              (i > 0 && glyph_infos[i].cluster != glyph_infos[i - 1].cluster);

        // Bbox lookup with separate mutex
        GlyphBBoxCacheKey key{fr.face, gp.glyph_id, pixel_size};
        {
            std::lock_guard<std::mutex> bbox_lock(m_impl->glyph_bbox_mutex);
            auto cached = m_impl->glyph_bbox_cache.get(key);
            if (cached) {
                gp.bbox_x0 = cached->x0;
                gp.bbox_y0 = cached->y0;
                gp.bbox_x1 = cached->x1;
                gp.bbox_y1 = cached->y1;
            } else {
                FT_Error err = FT_Load_Glyph(fr.face, gp.glyph_id, FT_LOAD_DEFAULT);
                if (err == 0) {
                    FT_GlyphSlot slot = fr.face->glyph;
                    gp.bbox_x0 = static_cast<float>(slot->metrics.horiBearingX) * fr.ft_scale;
                    gp.bbox_y0 = static_cast<float>(slot->metrics.horiBearingY) * fr.ft_scale;
                    gp.bbox_x1 = gp.bbox_x0 + static_cast<float>(slot->metrics.width) * fr.ft_scale;
                    gp.bbox_y1 = gp.bbox_y0 - static_cast<float>(slot->metrics.height) * fr.ft_scale;
                    m_impl->glyph_bbox_cache.put(key, GlyphBBoxCacheEntry{
                        gp.bbox_x0, gp.bbox_y0, gp.bbox_x1, gp.bbox_y1
                    }, 1);
                }
            }
        }

        run.glyphs.push_back(gp);
        cursor_x += gp.advance_x;
        cursor_y += gp.advance_y;
    }

    m_impl->release_buffer(buf);

    run.width = cursor_x;
    run.ascent  = static_cast<float>(fr.face->size->metrics.ascender)  * fr.ft_scale;
    run.descent = -static_cast<float>(fr.face->size->metrics.descender) * fr.ft_scale;
    run.baseline = 0.0f;
    run.line_height = static_cast<float>(fr.face->size->metrics.height) * fr.ft_scale;

    return run;
}

// ── measure_text (fast path) ──────────────────────────────────────────

float FontEngine::measure_text(std::string_view text, const FontSpec& spec, float font_size) const {
    // OPT: fast path — sum advances directly, skip bbox lookup & GlyphPosition creation
    if (!m_impl || !m_impl->ft_library || text.empty() || font_size <= 0.0f) {
        return 0.0f;
    }

    Impl::FaceResult fr = m_impl->acquire_face(spec, font_size);
    if (!fr.ok) return 0.0f;

    hb_buffer_t* buf = m_impl->acquire_buffer();
    if (!buf) return 0.0f;
    hb_buffer_add_utf8(buf, text.data(), static_cast<int>(text.size()), 0, static_cast<int>(text.size()));
    hb_buffer_set_direction(buf, HB_DIRECTION_LTR);
    hb_buffer_set_script(buf, HB_SCRIPT_LATIN);
    hb_buffer_set_language(buf, hb_language_from_string("en", -1));

    hb_shape(fr.entry->hb_font, buf, nullptr, 0);

    unsigned int glyph_count = 0;
    hb_glyph_position_t* glyph_positions = hb_buffer_get_glyph_positions(buf, &glyph_count);

    float total_width = 0.0f;
    for (unsigned int i = 0; i < glyph_count; ++i) {
        total_width += static_cast<float>(glyph_positions[i].x_advance) * fr.hb_scale;
    }

    m_impl->release_buffer(buf);
    return total_width;
}

// ── get_font_metrics (with cache) ─────────────────────────────────────

FontEngine::FontMetrics FontEngine::get_font_metrics(const FontSpec& spec, float font_size) const {
    FontMetrics metrics{};
    if (!m_impl || !m_impl->ft_library || font_size <= 0.0f) return metrics;

    // OPT: check metrics cache
    MetricsCacheKey mkey = make_metrics_key(spec, font_size);
    {
        std::shared_lock lock(m_impl->metrics_cache_mutex);
        auto cit = m_impl->metrics_cache.find(mkey);
        if (cit != m_impl->metrics_cache.end()) return cit->second;
    }

    Impl::FaceResult fr = m_impl->acquire_face(spec, font_size);
    if (!fr.ok) return metrics;

    metrics.ascent  = static_cast<float>(fr.face->size->metrics.ascender)  * fr.ft_scale;
    metrics.descent = -static_cast<float>(fr.face->size->metrics.descender) * fr.ft_scale;
    metrics.line_height = static_cast<float>(fr.face->size->metrics.height) * fr.ft_scale;

    FT_Load_Char(fr.face, 'x', FT_LOAD_DEFAULT);
    if (fr.face->glyph) metrics.x_height = static_cast<float>(fr.face->glyph->metrics.horiBearingY) * fr.ft_scale;

    FT_Load_Char(fr.face, 'H', FT_LOAD_DEFAULT);
    if (fr.face->glyph) metrics.cap_height = static_cast<float>(fr.face->glyph->metrics.horiBearingY) * fr.ft_scale;

    metrics.max_advance = static_cast<float>(fr.face->size->metrics.max_advance) * fr.ft_scale;

    // Store in cache
    {
        std::unique_lock lock(m_impl->metrics_cache_mutex);
        m_impl->metrics_cache[mkey] = metrics;
    }

    return metrics;
}

// ── Cache management ─────────────────────────────────────────────────

void FontEngine::clear_cache() {
    if (!m_impl) return;
    std::lock_guard<std::mutex> lock(m_impl->glyph_bbox_mutex);
    std::lock_guard<std::mutex> pool_lock(m_impl->buffer_pool_mutex);
    std::unique_lock face_lock(m_impl->face_cache_mutex);
    std::unique_lock metrics_lock(m_impl->metrics_cache_mutex);
    m_impl->clear_cache_unlocked();
}

size_t FontEngine::glyph_bbox_cache_size() const {
    if (!m_impl) return 0;
    std::lock_guard<std::mutex> lock(m_impl->glyph_bbox_mutex);
    return m_impl->glyph_bbox_cache.stats().current_size;
}

bool FontEngine::can_load(const FontSpec& spec) {
    if (!m_impl || !m_impl->ft_library) return false;

    std::shared_lock face_lock(m_impl->face_cache_mutex);
    auto it = m_impl->face_cache.find(spec);
    if (it != m_impl->face_cache.end()) return it->second.valid();
    face_lock.unlock();

    std::unique_lock wlock(m_impl->face_cache_mutex);
    auto it2 = m_impl->face_cache.find(spec);
    if (it2 != m_impl->face_cache.end()) return it2->second.valid();

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

void reset_shared_font_engine() {
    auto& engine = shared_font_engine();
    engine.clear_cache();
}

} // namespace chronon3d
