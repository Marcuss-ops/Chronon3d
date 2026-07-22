#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/cache/lru_cache.hpp>
#include <chronon3d/backends/text/text_layout_engine.hpp>
#include <chronon3d/text/typewriter_layout_cache.hpp>
#include <spdlog/spdlog.h>

// Cat-5 internal: definition of the cluster-fallback coverage probe free
// function. See `src/backends/text/font_engine_internal.hpp` for the
// consumer-facing declaration; the friend declaration on FontEngine
// grants access to `m_impl`.
#include "src/backends/text/font_engine_internal.hpp"

#ifdef CHRONON3D_ENABLE_TEXT
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_OUTLINE_H
#include FT_BBOX_H

#include <hb.h>
#include <hb-ft.h>
#endif

#include <cmath>
#include <algorithm>
#include <set>
#include <shared_mutex>
#include <unordered_map>
#include <cctype>   // std::isspace for the OpenType feature token parser (TICKET-OPENTYPE-FEATURES-PASS)
#include <vector>   // std::vector<hb_feature_t> return type for the parser

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
    std::string family_name;
    std::string style_name;
    bool has_kerning{false};
    FT_UInt notdef_glyph_id{0};

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

// ═══════════════════════════════════════════════════════════════════════════
// TICKET-OPENTYPE-FEATURES-PASS — canonical OpenType feature parser.
//
// Each comma-separated token in `spec` is delegated to HarfBuzz's
// native `hb_feature_from_string()` which understands the OT / HarfBuzz
// grammar: "liga", "liga=1", "liga=0", "-liga", "ss01=2", "kern=0",
// "-dlig", etc.  Whitespace around tokens is trimmed.  Empty input
// yields an empty vector, preserving the historical
// `hb_shape(buf, nullptr, 0)` semantics where HarfBuzz applies its
// implicit defaults (kern=1, liga=1, calt=1, ...).
//
// Threading downstream:
//
//   TextRunLayout::features  (= TextShapingFeatures type alias)
//     -> TextShaping::features            (set per-call by callers)
//       -> parse_opentype_features()      (this anon-namespace helper)
//          -> hb_shape(font, buf,
//                      features.data(),
//                      features.size())
//
// Malformed tokens are silently dropped (hb_feature_from_string returns
// false): the user spec calls fail-loud via diagnostics for compilation-
// level bugs but per-shape-call malformed tokens must not abort the
// whole pipeline (Cat-3 minimal-surface contract).
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
    /// WP-8 PR 8.0 — typed asset resolver, non-owning.  Lifetime is the
    /// owning runtime's responsibility; this pointer is captured at FontEngine
    /// construction and used by every `load_face()` call for relative font-path
    /// resolution.  The default FontEngine ctor delegates to the explicit ctor
    /// with `runtime::typed_resolver_for_deep_code()` (PR 8.0 transitional
    /// bridge — deleted in PR 8.1).
    const chronon3d::assets::AssetResolver* m_resolver{nullptr};
    mutable std::unordered_map<FontSpec, FaceEntry, std::hash<FontSpec>> face_cache;
    mutable std::shared_mutex face_cache_mutex;
    mutable cache::LruCache<GlyphBBoxCacheKey, GlyphBBoxCacheEntry, std::hash<GlyphBBoxCacheKey>> glyph_bbox_cache{8192, 2};
    std::unique_ptr<chronon3d::content::text::TypewriterLayoutCache> typewriter_layout_cache; // per-runtime

    // FASE 4 (TICKET-088) — HarfBuzz buffer pool.
    // `hb_buffer_create()` + `hb_buffer_destroy()` is a per-shape-call
    // overhead (alloc / free + HarfBuzz internal bookkeeping).  We pool
    // reset-able buffers (callers must `hb_buffer_reset(buf)` before
    // reuse; we do that on release) so steady-state shaping has zero
    // HarfBuzz allocation cost.  Pool is bounded by kMaxHbBufferPoolSize
    // to avoid runaway memory if a single burst creates many buffers.
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
        // Caller-owned reset point: wipe glyph infos / positions / flags /
        // cluster state so the next acquire starts from a clean slate.
        hb_buffer_reset(buf);
        std::lock_guard<std::mutex> lock(hb_buffer_pool_mutex);
        if (hb_buffer_pool.size() < kMaxHbBufferPoolSize) {
            hb_buffer_pool.push_back(buf);
        } else {
            // Pool full → destroy rather than leak.
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

    // Shared helper: load/cached a FaceEntry for `spec`. Non-owning pointer
    // to the cached entry, or nullptr if the font cannot be loaded. Follows
    // the same shared_mutex dance as FontEngine::can_load().
    [[nodiscard]] FaceEntry* get_face_entry(const FontSpec& spec) {
        if (!ft_library) return nullptr;

        {
            std::shared_lock<std::shared_mutex> shared_lock(face_cache_mutex);
            auto it = face_cache.find(spec);
            if (it != face_cache.end()) {
                return it->second.valid() ? &it->second : nullptr;
            }
        }
        std::unique_lock<std::shared_mutex> unique_lock(face_cache_mutex);
        auto it = face_cache.find(spec);
        if (it == face_cache.end()) {
            auto entry = load_face(spec);
            if (!entry) return nullptr;
            auto [inserted_it, ok] = face_cache.emplace(spec, std::move(*entry));
            if (!ok) return nullptr;
            it = inserted_it;
        }
        return it->second.valid() ? &it->second : nullptr;
    }

    Impl() {
        FT_Error err = FT_Init_FreeType(&ft_library);
        if (err != 0) {
            spdlog::error("FontEngine: FT_Init_FreeType failed (error={})", err);
            ft_library = nullptr;
        }
        typewriter_layout_cache = std::make_unique<chronon3d::content::text::TypewriterLayoutCache>();
    }

    explicit Impl(const chronon3d::assets::AssetResolver* resolver)
        : m_resolver(resolver) {
        FT_Error err = FT_Init_FreeType(&ft_library);
        if (err != 0) {
            spdlog::error("FontEngine: FT_Init_FreeType failed (error={})", err);
            ft_library = nullptr;
        }
        typewriter_layout_cache = std::make_unique<chronon3d::content::text::TypewriterLayoutCache>();
    }

    ~Impl() {
        destroy_hb_buffer_pool();
        clear_cache_unlocked();
        if (ft_library) {
            FT_Done_FreeType(ft_library);
        }
    }

    void clear_cache_unlocked() {
        if (typewriter_layout_cache) {
            typewriter_layout_cache->clear();
        }
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

        // WP-8 PR 8.0 — resolver is owned by the FontEngine (pointer
        // captured at construction).  `m_resolver` is never null in any
        // context that reaches load_face: the explicit ctor requires a
        // resolver; the transitional default ctor populates it with
        // `runtime::typed_resolver_for_deep_code()`.  The 2-line fallback
        // preserves legacy `runtime::resolve_asset_path(relative)`
        // semantics for empty/unmounted-relative inputs.
        std::string resolved;
        const auto& resolver = *m_resolver;
        if (auto opt = resolver.resolve_lexical(spec.font_path)) {
            resolved = opt->string();
        } else {
            resolved = spec.font_path.empty() ? std::string{}
                                              : std::string{spec.font_path};
        }
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
        entry.family_name = face->family_name ? face->family_name : std::string{};
        entry.style_name = face->style_name ? face->style_name : std::string{};
        entry.has_kerning = FT_HAS_KERNING(face);
        // Cache the .notdef glyph index so the cluster fallback probe can
        // distinguish a real glyph from the fallback .notdef glyph. Some
        // fonts map missing codepoints to a non-zero .notdef index; checking
        // only glyph_index != 0 is not enough in those cases.
        // Resolve the .notdef glyph by its canonical PostScript name rather
        // than by querying U+0000, which may map to a different glyph or to
        // an empty cmap entry in some fonts.
        FT_Int notdef_idx = FT_Get_Name_Index(face, const_cast<FT_String*>(".notdef"));
        entry.notdef_glyph_id = (notdef_idx >= 0) ? static_cast<FT_UInt>(notdef_idx) : 0;

        return entry;
    }

    [[nodiscard]] static float pixel_scale_26_6() {
        return 1.0f / 64.0f;
    }
};

// ── FontEngine public API (full) ─────────────────────────────────────

// WP-8 PR 8.0 — explicit-resolver ctor (canonical).
FontEngine::FontEngine(const chronon3d::assets::AssetResolver& resolver)
    : m_impl(std::make_unique<Impl>(&resolver)) {}

// =====================================================================

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

    // TICKET-OPENTYPE-FEATURES-PASS — thread OpenType features explicitly
    // to HarfBuzz (parsed by the canonical anon-namespace helper above).
    // `shaping.features` e.g. "kern=1,liga=0" -> hb_feature_t array;
    // empty string -> empty array -> hb_shape(buf, features.data(), 0)
    // ≡ historical hb_shape(buf, nullptr, 0) (HarfBuzz default-on).
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
                if (slot->format == FT_GLYPH_FORMAT_OUTLINE) {
                    // Geometric outline bbox (FreeType 26.6 fixed coords).
                    // This is the authoritative ink extent of the glyph
                    // outline, superseding the grid-fitted slot->metrics
                    // used for typographic advance. Using the real outline
                    // closes the POST_RENDER_EXPAND gap where rasterized
                    // ink spilled past the predicted bbox.
                    FT_BBox outline_bbox;
                    FT_Outline_Get_BBox(&slot->outline, &outline_bbox);
                    gp.bbox_x0 = static_cast<float>(outline_bbox.xMin) * scale;
                    gp.bbox_y0 = static_cast<float>(outline_bbox.yMax) * scale;
                    gp.bbox_x1 = static_cast<float>(outline_bbox.xMax) * scale;
                    gp.bbox_y1 = static_cast<float>(outline_bbox.yMin) * scale;
                } else {
                    // Fallback for bitmap/emoji glyphs (CBDT/SBIX etc.)
                    // where an outline bbox is unavailable.
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

    // FASE 4 (TICKET-088) — release the buffer to the per-Impl pool
    // (reset + cache, NOT destroyed) on the success path.  Failures
    // before the release point also call release_hb_buffer so the
    // normalize handles both clean shutdown and bail-out paths.
    m_impl->release_hb_buffer(buf);

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

chronon3d::content::text::TypewriterLayoutCache& FontEngine::typewriter_layout_cache() {
    if (!m_impl || !m_impl->typewriter_layout_cache) {
        throw std::runtime_error("FontEngine::typewriter_layout_cache() called on an invalid engine");
    }
    return *m_impl->typewriter_layout_cache;
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

// Cat-5: glyph coverage probe is a free function in the
// `font_engine_internal` namespace, friend-declared on FontEngine. The
// class itself does NOT expose `has_glyph_for_codepoint` as a public
// method, keeping the public ABI minimal.
namespace {

// Codepoints that legitimately carry no visible ink (space, control,
// line-break, joiners, etc.). Their glyphs are expected to have empty
// outlines, so the coverage probe must not reject them on that basis.
bool is_invisible_codepoint(char32_t cp) noexcept {
    // ASCII whitespace / control.
    if (cp <= 0x1F || (cp >= 0x7F && cp <= 0x9F)) return true;
    if (cp == 0x20 || cp == 0xA0) return true;

    // Unicode line/paragraph separators and general punctuation spaces.
    if (cp == 0x2028 || cp == 0x2029) return true;
    if (cp >= 0x2000 && cp <= 0x200D) return true;  // spaces + ZWNJ/ZWJ
    if (cp == 0x202F || cp == 0x205F || cp == 0x2060) return true;
    if (cp == 0xFEFF) return true;                    // BOM / zero-width no-break

    return false;
}

} // anonymous namespace

namespace text::font_engine_internal {

bool has_glyph_for_codepoint(FontEngine& engine, const FontSpec& spec, char32_t codepoint) {
#ifdef CHRONON3D_ENABLE_TEXT
    if (!engine.m_impl) return false;
    FaceEntry* entry = engine.m_impl->get_face_entry(spec);
    if (!entry) return false;

    // Use FreeType's strict cmap resolver instead of HarfBuzz's nominal probe.
    // Some fonts map missing codepoints to a non-zero .notdef glyph, so we
    // also reject glyphs that resolve to the cached .notdef index.
    FT_UInt glyph_index = FT_Get_Char_Index(entry->ft_face, static_cast<FT_ULong>(codepoint));
    if (glyph_index == 0 || glyph_index == entry->notdef_glyph_id) {
        return false;
    }

    // Some widely-used system/web fonts map unsupported codepoints to an
    // empty space-like fallback glyph. For visible characters we therefore
    // verify the glyph actually carries outline or bitmap ink; invisible
    // characters (space, joiners, controls) are exempt because their real
    // glyphs are expected to be empty.
    if (is_invisible_codepoint(codepoint)) {
        return true;
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
    FaceEntry* entry = engine.m_impl->get_face_entry(spec);
    if (!entry) return false;

    out_family = entry->family_name;
    out_style  = entry->style_name;
    out_weight = entry->font_weight;

    // Simple heuristic for weight/style from the face metadata.
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

// =============================================================================
// Stub implementation (no FreeType / HarfBuzz available)
// =============================================================================
#else

struct FontEngine::Impl {
    const chronon3d::assets::AssetResolver* m_resolver{nullptr};
    std::unique_ptr<chronon3d::content::text::TypewriterLayoutCache> typewriter_layout_cache;
    ~Impl() = default;

    explicit Impl(const chronon3d::assets::AssetResolver* resolver)
        : m_resolver(resolver) {
        typewriter_layout_cache = std::make_unique<chronon3d::content::text::TypewriterLayoutCache>();
    }
};

FontEngine::FontEngine(const chronon3d::assets::AssetResolver& resolver)
    : m_impl(std::make_unique<Impl>(&resolver)) {}

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

// Cat-5: stub for the cluster-fallback coverage probe (no FreeType /
// HarfBuzz available). Must mirror the full namespace declaration so
// the symbol exists in both modes.
namespace chronon3d::text::font_engine_internal {

bool has_glyph_for_codepoint(FontEngine&, const FontSpec&, char32_t) { return false; }

} // namespace chronon3d::text::font_engine_internal

// ── resolve_placed_glyph_run (extracted to font_engine_placed_run.cpp) ────
// FASE 14 — see src/backends/text/font_engine_placed_run.cpp

} // namespace chronon3d
