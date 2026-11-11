#pragma once

#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/cache/lru_cache.hpp>
#include <chronon3d/backends/text/text_layout_engine.hpp>
#include <chronon3d/text/typewriter_layout_cache.hpp>
#include <chronon3d/text/text_layout_cache.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/hash/hash_builder.hpp>
#include "font_bbox_cache_key.hpp"
#include "src/backends/text/font_engine_internal.hpp"

#ifdef CHRONON3D_ENABLE_TEXT
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_OUTLINE_H
#include FT_BBOX_H
#include FT_MULTIPLE_MASTERS_H
#include <hb.h>
#include <hb-ft.h>
#include <hb-ot.h>
#include "font_variations_internal.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <limits>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace chronon3d {

namespace {
bool is_invisible_codepoint(char32_t cp) noexcept {
    return cp <= 0x20 ||
        (cp >= 0x7F && cp <= 0xA0) ||
        (cp >= 0x2000 && cp <= 0x200D) ||
        (cp >= 0x2028 && cp <= 0x2029) ||
        cp == 0x202F || cp == 0x205F || cp == 0x2060 || cp == 0xFEFF;
}

} // namespace

/// Lifetime-owned FreeType/HarfBuzz face.  FontEngine operations hold a
/// shared_ptr lease for the whole operation, so cache eviction can drop its
/// ownership without destroying a face that is still being shaped or probed.
struct FaceEntry {
    FontFaceId id{0};
    // FT_New_Memory_Face keeps a pointer to its input for the lifetime of the
    // face. Owning the bytes here also lets Windows replace/update a font file
    // while the cached face is live (FT_New_Face leaves the file mapped).
    std::vector<FT_Byte> font_data;
    FT_Face ft_face{nullptr};
    hb_font_t* hb_font{nullptr};
    std::string resolved_path;
    int font_weight{400};
    std::string family_name;
    std::string style_name;
    bool has_kerning{false};
    FT_UInt notdef_glyph_id{0};
    // FreeType faces are shared and mutable. Protected by ops_mutex.
    std::string active_variation_axes;
    // FreeType face objects carry mutable size/glyph state. The lock belongs
    // to the face, not to the whole FontEngine, so unrelated faces can be
    // shaped/rasterized concurrently while preserving face safety.
    mutable std::mutex ops_mutex;

    FaceEntry() = default;
    FaceEntry(const FaceEntry&) = delete;
    FaceEntry& operator=(const FaceEntry&) = delete;

    ~FaceEntry() {
        if (hb_font) hb_font_destroy(hb_font);
        if (ft_face) FT_Done_Face(ft_face);
    }

    bool valid() const { return ft_face != nullptr && hb_font != nullptr; }
};

namespace {
bool apply_variation_axes(FaceEntry& entry, FT_Library library,
                          const std::string& axes) {
    if (entry.active_variation_axes == axes) return true;
    std::vector<text_internal::FontVariationCoordinate> requested;
    std::vector<text_internal::FontVariationCoordinate> effective;
    if (!text_internal::parse_font_variation_axes(axes, requested) ||
        !text_internal::apply_font_variation_coordinates(
            library, entry.ft_face, requested, &effective)) return false;

    hb_ft_font_changed(entry.hb_font);
    std::vector<hb_variation_t> hb_variations;
    hb_variations.reserve(effective.size());
    for (const auto& variation : effective) {
        hb_variations.push_back({static_cast<hb_tag_t>(variation.tag), variation.value});
    }
    hb_font_set_variations(entry.hb_font, hb_variations.data(),
                           static_cast<unsigned int>(hb_variations.size()));
    entry.active_variation_axes = axes;
    return true;
}
} // namespace

struct GlyphBBoxCacheEntry {
    float x0{0.0f};
    float y0{0.0f};
    float x1{0.0f};
    float y1{0.0f};
};

struct FontFaceCacheKey {
    FontSpec spec{};
    std::string resolved_path;
    std::uintmax_t byte_size{0};
    std::int64_t write_time_ticks{0};

    friend bool operator==(const FontFaceCacheKey& a,
                           const FontFaceCacheKey& b) noexcept {
        return a.spec == b.spec && a.resolved_path == b.resolved_path &&
            a.byte_size == b.byte_size && a.write_time_ticks == b.write_time_ticks;
    }
};

struct FontFaceCacheKeyHash {
    [[nodiscard]] std::size_t operator()(const FontFaceCacheKey& key) const noexcept {
        return static_cast<std::size_t>(core::hash::HashBuilder{}
            .add("chronon.font-face-cache.v2")
            .add(key.spec.font_path).add(key.spec.font_family)
            .add(key.spec.font_weight).add(key.spec.font_style)
            .add(key.resolved_path).add(key.byte_size).add(key.write_time_ticks)
            .finish());
    }
};

} // namespace chronon3d

// ═══════════════════════════════════════════════════════════════════════════
// Parse the OpenType feature string before passing explicit features to HarfBuzz.
// ═══════════════════════════════════════════════════════════════════════════
namespace {
hb_bool_t reject_unicode_composition(hb_unicode_funcs_t*, hb_codepoint_t,
                                     hb_codepoint_t, hb_codepoint_t*, void*) {
    return false;
}

hb_unicode_funcs_t* mark_paint_unicode_funcs() {
    // HarfBuzz normally NFC-composes a base plus combining mark before
    // OpenType shaping. SVG can assign those source characters different
    // paints, so this variant preserves the decomposed input while retaining
    // the font's mark positioning lookups.
    static hb_unicode_funcs_t* funcs = [] {
        auto* value = hb_unicode_funcs_create(hb_unicode_funcs_get_default());
        hb_unicode_funcs_set_compose_func(value, reject_unicode_composition,
                                          nullptr, nullptr);
        hb_unicode_funcs_make_immutable(value);
        return value;
    }();
    return funcs;
}

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

namespace chronon3d {

struct FontEngine::Impl {
    FT_Library ft_library{nullptr};
    const chronon3d::assets::AssetResolver* m_resolver{nullptr};

    // Canonical bounded face cache.  Values are shared lifetime leases rather
    // than by-value FT_Face owners; eviction only drops cache ownership.
    static constexpr size_t kMaxFaceCacheEntries = 128;
    mutable cache::LruCache<
        FontFaceCacheKey,
        std::shared_ptr<FaceEntry>,
        FontFaceCacheKeyHash> face_cache{
            kMaxFaceCacheEntries, 4, cache::CapacityMode::Count};
    mutable std::mutex face_load_mutex;
    mutable std::atomic<FontFaceId> next_face_id{1};

    mutable cache::LruCache<GlyphBBoxCacheKey, GlyphBBoxCacheEntry, GlyphBBoxCacheKeyHash> glyph_bbox_cache{8192, 2};
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
            [this](const FontFaceCacheKey&,
                   const std::shared_ptr<FaceEntry>& entry,
                   cache::CacheRemovalReason) {
                if (entry) invalidate_glyph_bboxes(entry->id);
            });
    }

    /// Return an owning lease.  Loading is serialized only on misses; normal
    /// cache hits use the canonical sharded LRU's own narrow locks.
    [[nodiscard]] std::shared_ptr<FaceEntry> get_face_entry(const FontSpec& spec) {
        if (!ft_library) return {};
        const auto key = make_face_cache_key(spec);

        if (auto cached = face_cache.get(key); cached) {
            if (profiling::g_current_counters) {
                profiling::g_current_counters->font_cache_hits.fetch_add(1, std::memory_order_relaxed);
            }
            return (*cached && (*cached)->valid()) ? *cached : std::shared_ptr<FaceEntry>{};
        }

        std::lock_guard<std::mutex> load_lock(face_load_mutex);
        if (auto cached = face_cache.get(key); cached) {
            if (profiling::g_current_counters) {
                profiling::g_current_counters->font_cache_hits.fetch_add(1, std::memory_order_relaxed);
            }
            return (*cached && (*cached)->valid()) ? *cached : std::shared_ptr<FaceEntry>{};
        }

        if (profiling::g_current_counters) {
            profiling::g_current_counters->font_cache_misses.fetch_add(1, std::memory_order_relaxed);
        }
        auto entry = load_face(spec, key.resolved_path);
        if (!entry || !entry->valid()) return {};
        if (!(make_face_cache_key(spec) == key)) return {};
        face_cache.put(key, entry);
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

    [[nodiscard]] std::string resolve_font_path(const FontSpec& spec) const {
        std::string resolved;
        const auto font_path = std::filesystem::path{spec.font_path};
        if (font_path.is_absolute()) {
            resolved = font_path.lexically_normal().string();
        } else if (m_resolver) {
            if (auto opt = m_resolver->resolve_lexical(font_path)) {
                resolved = opt->string();
            }
        }
        return resolved.empty() ? spec.font_path : resolved;
    }

    [[nodiscard]] FontFaceCacheKey make_face_cache_key(const FontSpec& spec) const {
        auto resolved = resolve_font_path(spec);
        std::error_code size_error;
        const auto size = std::filesystem::file_size(resolved, size_error);
        std::error_code time_error;
        const auto write_time = std::filesystem::last_write_time(resolved, time_error);
        return FontFaceCacheKey{
            spec,
            std::move(resolved),
            size_error ? 0 : size,
            time_error ? 0 : static_cast<std::int64_t>(write_time.time_since_epoch().count())};
    }

    std::shared_ptr<FaceEntry> load_face(const FontSpec& spec,
                                         const std::string& resolved) {
        if (!ft_library) return {};
        const auto resolve_start = profiling::now();
        auto entry = std::make_shared<FaceEntry>();
        std::ifstream input(std::filesystem::path{resolved},
                            std::ios::binary | std::ios::ate);
        if (!input) {
            spdlog::warn("FontEngine: failed to open font '{}' (resolved: '{}')",
                         spec.font_path, resolved);
            return {};
        }
        const auto end = input.tellg();
        const auto file_size = static_cast<std::streamoff>(end);
        if (file_size <= 0 || static_cast<std::uintmax_t>(file_size) >
                            static_cast<std::uintmax_t>(std::numeric_limits<FT_Long>::max())) {
            spdlog::warn("FontEngine: font '{}' has an invalid or unsupported size",
                         resolved);
            return {};
        }
        entry->font_data.resize(static_cast<std::size_t>(file_size));
        input.seekg(0, std::ios::beg);
        if (!input.read(reinterpret_cast<char*>(entry->font_data.data()),
                        static_cast<std::streamsize>(entry->font_data.size()))) {
            spdlog::warn("FontEngine: failed to read font '{}'", resolved);
            return {};
        }

        FT_Face face = nullptr;
        FT_Error err = FT_New_Memory_Face(
            ft_library, entry->font_data.data(),
            static_cast<FT_Long>(entry->font_data.size()), 0, &face);
        if (err != 0) {
            // A missing font is retried once per text span: an overlay render
            // emitted tens of thousands of identical lines (60 GB of syslog in
            // one day). Warn once per resolved path per thread; a genuinely
            // different failure still gets its own line.
            static thread_local std::set<std::string> warned_missing_fonts;
            if (warned_missing_fonts.insert(resolved).second) {
                spdlog::warn("FontEngine: failed to load font '{}' (resolved: '{}', error={})",
                             spec.font_path, resolved, err);
            }
            return {};
        }

        FT_Set_Pixel_Sizes(face, 0, 16);

        hb_font_t* hb_font = hb_ft_font_create(face, nullptr);
        if (!hb_font) {
            FT_Done_Face(face);
            return {};
        }

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

} // namespace chronon3d

#endif // CHRONON3D_ENABLE_TEXT
