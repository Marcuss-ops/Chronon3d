#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/cache/lru_cache.hpp>
#include <chronon3d/backends/text/text_layout_engine.hpp>
#include <chronon3d/text/typewriter_layout_cache.hpp>
#include <chronon3d/text/text_layout_cache.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include "src/backends/text/font_engine_internal.hpp"

#ifdef CHRONON3D_ENABLE_TEXT
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_OUTLINE_H
#include FT_BBOX_H
#include <hb.h>
#include <hb-ft.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <memory>
#include <set>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace chronon3d {

namespace {
bool is_invisible_codepoint(char32_t cp) noexcept {
    if (cp <= 0x1F || (cp >= 0x7F && cp <= 0x9F)) return true;
    if (cp == 0x20 || cp == 0xA0) return true;
    if (cp == 0x2028 || cp == 0x2029) return true;
    if (cp >= 0x2000 && cp <= 0x200D) return true;
    if (cp == 0x202F || cp == 0x205F || cp == 0x2060) return true;
    return cp == 0xFEFF;
}
} // namespace

using FontFaceId = std::uint64_t;

/// Lifetime-owned FreeType/HarfBuzz face.  FontEngine operations hold a
/// shared_ptr lease for the whole operation, so cache eviction can drop its
/// ownership without destroying a face that is still being shaped or probed.
struct FaceEntry {
    FontFaceId id{0};
    FT_Face ft_face{nullptr};
    hb_font_t* hb_font{nullptr};
    std::string resolved_path;
    int font_weight{400};
    std::string family_name;
    std::string style_name;
    bool has_kerning{false};
    FT_UInt notdef_glyph_id{0};

    FaceEntry() = default;
    FaceEntry(const FaceEntry&) = delete;
    FaceEntry& operator=(const FaceEntry&) = delete;

    ~FaceEntry() {
        if (hb_font) hb_font_destroy(hb_font);
        if (ft_face) FT_Done_Face(ft_face);
    }

    bool valid() const { return ft_face != nullptr && hb_font != nullptr; }
};

struct GlyphBBoxCacheKey {
    FontFaceId face_id{0};
    u32        glyph_id{0};
    u32        pixel_size{0};

    bool operator==(const GlyphBBoxCacheKey& o) const noexcept {
        return face_id == o.face_id && glyph_id == o.glyph_id && pixel_size == o.pixel_size;
    }
};

struct GlyphBBoxCacheEntry {
    float x0{0.0f};
    float y0{0.0f};
    float x1{0.0f};
    float y1{0.0f};
};

} // namespace chronon3d

// ═══════════════════════════════════════════════════════════════════════════
// TICKET-OPENTYPE-FEATURES-PASS — canonical OpenType feature parser.
// ═══════════════════════════════════════════════════════════════════════════
namespace {
[[nodiscard]] std::vector<hb_feature_t>
parse_opentype_features(std::string_view spec) {
    std::vector<hb_feature_t> features;
    if (spec.empty()) return features;

    std::size_t start = 0;
    while (start <= spec.size()) {
        const std::size_t pos = spec.find(',', start);
        const std::size_t end =
            (pos == std::string_view::npos) ? spec.size() : pos;

        std::string_view token = spec.substr(start, end - start);
        while (!token.empty() &&
               std::isspace(static_cast<unsigned char>(token.front()))) {
            token.remove_prefix(1);
        }
        while (!token.empty() &&
               std::isspace(static_cast<unsigned char>(token.back()))) {
            token.remove_suffix(1);
        }

        if (!token.empty()) {
            hb_feature_t feature{};
            if (hb_feature_from_string(
                    token.data(),
                    static_cast<int>(token.size()),
                    &feature)) {
                features.push_back(feature);
            }
        }

        if (end == spec.size()) break;
        start = end + 1;
    }
    return features;
}
} // anonymous namespace

namespace std {
template<> struct hash<chronon3d::GlyphBBoxCacheKey> {
    [[nodiscard]] size_t operator()(const chronon3d::GlyphBBoxCacheKey& k) const noexcept {
        size_t h = 0;
        h ^= std::hash<chronon3d::FontFaceId>{}(k.face_id) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<chronon3d::u32>{}(k.glyph_id) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<chronon3d::u32>{}(k.pixel_size) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};
} // namespace std

namespace chronon3d {

struct FontEngine::Impl {
    FT_Library ft_library{nullptr};
    const chronon3d::assets::AssetResolver* m_resolver{nullptr};

    // Canonical bounded face cache.  Values are shared lifetime leases rather
    // than by-value FT_Face owners; eviction only drops cache ownership.
    static constexpr size_t kMaxFaceCacheEntries = 128;
    mutable cache::LruCache<
        FontSpec,
        std::shared_ptr<FaceEntry>,
        std::hash<FontSpec>> face_cache{
            kMaxFaceCacheEntries, 4, cache::CapacityMode::Count};
    mutable std::mutex face_load_mutex;
    mutable std::atomic<FontFaceId> next_face_id{1};

    // Protect mutable FT_Face/HarfBuzz state; distinct from cache ownership.
    mutable std::mutex font_ops_mutex;
    mutable cache::LruCache<GlyphBBoxCacheKey, GlyphBBoxCacheEntry, std::hash<GlyphBBoxCacheKey>> glyph_bbox_cache{8192, 2};
    std::unique_ptr<chronon3d::content::text::TypewriterLayoutCache> typewriter_layout_cache;

    static constexpr size_t kMaxHbBufferPoolSize = 64;
    mutable std::mutex hb_buffer_pool_mutex;
    mutable std::vector<hb_buffer_t*> hb_buffer_pool;

    [[nodiscard]] hb_buffer_t* acquire_hb_buffer() const {
        std::lock_guard<std::mutex> lock(hb_buffer_pool_mutex);
        if (!hb_buffer_pool.empty()) {
            hb_buffer_t* buf = hb_buffer_pool.back();
            hb_buffer_pool.pop_back();
            return buf;
        }
        return hb_buffer_create();
    }

    void release_hb_buffer(hb_buffer_t* buf) const {
        if (!buf) return;
        hb_buffer_reset(buf);
        std::lock_guard<std::mutex> lock(hb_buffer_pool_mutex);
        if (hb_buffer_pool.size() < kMaxHbBufferPoolSize) {
            hb_buffer_pool.push_back(buf);
        } else {
            hb_buffer_destroy(buf);
        }
    }

    void destroy_hb_buffer_pool() {
        std::lock_guard<std::mutex> lock(hb_buffer_pool_mutex);
        for (hb_buffer_t* buf : hb_buffer_pool) {
            if (buf) hb_buffer_destroy(buf);
        }
        hb_buffer_pool.clear();
    }

    void invalidate_glyph_bboxes(FontFaceId face_id) {
        if (face_id == 0) return;
        std::vector<GlyphBBoxCacheKey> stale;
        glyph_bbox_cache.for_each([&](const GlyphBBoxCacheKey& key,
                                      const GlyphBBoxCacheEntry&,
                                      std::size_t) {
            if (key.face_id == face_id) stale.push_back(key);
        });
        for (const auto& key : stale) {
            (void)glyph_bbox_cache.erase(key, false);
        }
    }

    void install_face_cache_removal_callback() {
        face_cache.set_removal_callback(
            [this](const FontSpec&,
                   const std::shared_ptr<FaceEntry>& entry,
                   cache::CacheRemovalReason) {
                if (entry) invalidate_glyph_bboxes(entry->id);
            });
    }

    /// Return an owning lease.  Loading is serialized only on misses; normal
    /// cache hits use the canonical sharded LRU's own narrow locks.
    [[nodiscard]] std::shared_ptr<FaceEntry> get_face_entry(const FontSpec& spec) {
        if (!ft_library) return {};

        if (auto cached = face_cache.get(spec); cached) {
            return (*cached && (*cached)->valid()) ? *cached : std::shared_ptr<FaceEntry>{};
        }

        std::lock_guard<std::mutex> load_lock(face_load_mutex);
        if (auto cached = face_cache.get(spec); cached) {
            return (*cached && (*cached)->valid()) ? *cached : std::shared_ptr<FaceEntry>{};
        }

        auto entry = load_face(spec);
        if (!entry || !entry->valid()) return {};
        face_cache.put(spec, entry);
        return entry;
    }

    Impl() {
        FT_Error err = FT_Init_FreeType(&ft_library);
        if (err != 0) {
            spdlog::error("FontEngine: FT_Init_FreeType failed (error={})", err);
            ft_library = nullptr;
        }
        install_face_cache_removal_callback();
        typewriter_layout_cache = std::make_unique<chronon3d::content::text::TypewriterLayoutCache>();
    }

    explicit Impl(const chronon3d::assets::AssetResolver* resolver)
        : m_resolver(resolver) {
        FT_Error err = FT_Init_FreeType(&ft_library);
        if (err != 0) {
            spdlog::error("FontEngine: FT_Init_FreeType failed (error={})", err);
            ft_library = nullptr;
        }
        install_face_cache_removal_callback();
        typewriter_layout_cache = std::make_unique<chronon3d::content::text::TypewriterLayoutCache>();
    }

    ~Impl() {
        destroy_hb_buffer_pool();
        clear_cache_unlocked();
        // Removal callbacks capture this; detach before members begin their
        // automatic destruction sequence.
        face_cache.set_removal_callback({});
        if (ft_library) {
            FT_Done_FreeType(ft_library);
        }
    }

    void clear_cache_unlocked() {
        if (typewriter_layout_cache) {
            typewriter_layout_cache->clear();
        }
        face_cache.clear();
        glyph_bbox_cache.clear();
    }

    std::shared_ptr<FaceEntry> load_face(const FontSpec& spec) {
        if (!ft_library) return {};

        const auto resolve_start = profiling::now();

        std::string resolved;
        const auto font_path = std::filesystem::path{spec.font_path};
        if (font_path.is_absolute()) {
            resolved = font_path.lexically_normal().string();
        } else {
            const auto& resolver = *m_resolver;
            if (auto opt = resolver.resolve_lexical(font_path)) {
                resolved = opt->string();
            } else {
                resolved = spec.font_path.empty() ? std::string{}
                                                  : std::string{spec.font_path};
            }
        }
        if (resolved.empty()) {
            resolved = spec.font_path;
        }
        FT_Face face = nullptr;
        FT_Error err = FT_New_Face(ft_library, resolved.c_str(), 0, &face);
        if (err != 0) {
            spdlog::warn("FontEngine: failed to load font '{}' (resolved: '{}', error={})",
                         spec.font_path, resolved, err);
            return {};
        }

        FT_Set_Pixel_Sizes(face, 0, 16);

        hb_font_t* hb_font = hb_ft_font_create(face, nullptr);
        if (!hb_font) {
            FT_Done_Face(face);
            return {};
        }

        auto entry = std::make_shared<FaceEntry>();
        entry->id = next_face_id.fetch_add(1, std::memory_order_relaxed);
        entry->ft_face = face;
        entry->hb_font = hb_font;
        entry->resolved_path = std::move(resolved);
        entry->font_weight = spec.font_weight;
        entry->family_name = face->family_name ? face->family_name : std::string{};
        entry->style_name = face->style_name ? face->style_name : std::string{};
        entry->has_kerning = FT_HAS_KERNING(face);
        FT_Int notdef_idx = FT_Get_Name_Index(face, const_cast<FT_String*>(".notdef"));
        entry->notdef_glyph_id = (notdef_idx >= 0) ? static_cast<FT_UInt>(notdef_idx) : 0;

        if (profiling::g_current_counters) {
            profiling::g_current_counters->font_resolve_wall_us.fetch_add(
                static_cast<uint64_t>(std::llround(profiling::elapsed_us(resolve_start))),
                std::memory_order_relaxed);
        }

        return entry;
    }

    [[nodiscard]] static float pixel_scale_26_6() {
        return 1.0f / 64.0f;
    }
};

FontEngine::FontEngine(const chronon3d::assets::AssetResolver& resolver)
    : m_impl(std::make_unique<Impl>(&resolver)),
      m_text_layout_cache(std::make_unique<TextLayoutCache>()) {}

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

    const auto shaping_start = profiling::now();

    auto entry = m_impl->get_face_entry(spec);
    if (!entry || !entry->valid()) return std::nullopt;
    std::lock_guard<std::mutex> font_lock(m_impl->font_ops_mutex);

    FT_Face face = entry->ft_face;
    const float scale = Impl::pixel_scale_26_6();

    FT_Error size_err = FT_Set_Pixel_Sizes(face, 0, static_cast<unsigned int>(std::ceil(font_size)));
    if (size_err != 0) return std::nullopt;
    hb_ft_font_changed(entry->hb_font);

    hb_buffer_t* buf = m_impl->acquire_hb_buffer();
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

        GlyphBBoxCacheKey key{entry->id, gp.glyph_id, pixel_size};
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

    run.width = cursor_x;
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
    std::lock_guard<std::mutex> font_lock(m_impl->font_ops_mutex);

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

TextLayoutCache& FontEngine::text_layout_cache() noexcept {
    return *m_text_layout_cache;
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
    std::lock_guard<std::mutex> font_lock(engine.m_impl->font_ops_mutex);

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
