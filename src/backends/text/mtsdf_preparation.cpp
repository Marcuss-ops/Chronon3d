#include <chronon3d/text/mtsdf_preparation.hpp>

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/backends/text/mtsdf_generator.hpp>
#include <chronon3d/cache/lru_cache.hpp>

#ifdef CHRONON3D_ENABLE_TEXT
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H
#include "font_variations_internal.hpp"
#endif

#include <cstddef>
#include <algorithm>
#include <filesystem>
#include <optional>
#include <mutex>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace chronon3d::text {
namespace {

#ifdef CHRONON3D_ENABLE_TEXT
struct FaceEntry {
    FT_Face face{nullptr};
    std::mutex mutex;

    FaceEntry() = default;

    ~FaceEntry() {
        if (face) FT_Done_Face(face);
    }
    FaceEntry(const FaceEntry&) = delete;
    FaceEntry& operator=(const FaceEntry&) = delete;
};

FT_Library shared_library() {
    static FT_Library library = [] {
        FT_Library result = nullptr;
        if (FT_Init_FreeType(&result) != 0) return FT_Library{nullptr};
        return result;
    }();
    return library;
}
#endif

// P0-MTSDF: glyph identity is content-addressed, not path-addressed.
// The prepared font/content identity is the canonical key so that a daemon
// that sees /assets/foo.ttf change bytes mid-life does not return stale
// glyphs keyed only by path.
struct GlyphKey {
    assets::ContentDigest font_digest{};
    std::uint32_t glyph_id{0};
    std::string variation_axes;
    MtsdfProfile profile{};

    friend bool operator==(const GlyphKey&, const GlyphKey&) = default;
};

struct GlyphKeyHash {
    std::size_t operator()(const GlyphKey& key) const noexcept {
        std::size_t value = 0;
        for (auto b : key.font_digest.bytes) {
            value ^= static_cast<std::size_t>(std::to_integer<unsigned char>(b)) +
                0x9e3779b97f4a7c15ULL + (value << 6U) + (value >> 2U);
        }
        value ^= static_cast<std::size_t>(key.glyph_id) +
            0x9e3779b97f4a7c15ULL + (value << 6U) + (value >> 2U);
        for (const unsigned char c : key.variation_axes) {
            value ^= static_cast<std::size_t>(c) +
                0x9e3779b97f4a7c15ULL + (value << 6U) + (value >> 2U);
        }
        value ^= static_cast<std::size_t>(key.profile.em_px) +
            (value << 6U) + (value >> 2U);
        value ^= static_cast<std::size_t>(key.profile.distance_range_px) +
            (value << 6U) + (value >> 2U);
        value ^= static_cast<std::size_t>(key.profile.padding_px) +
            (value << 6U) + (value >> 2U);
        value ^= static_cast<std::size_t>(key.profile.generation_id) +
            (value << 6U) + (value >> 2U);
        return value;
    }
};

#ifdef CHRONON3D_ENABLE_TEXT
bool apply_face_variations(FT_Face face, FT_Library library,
                           std::string_view axes) {
    return text_internal::apply_font_variation_axes(library, face, axes);
}
#endif

// Canonical font content identity — ContentCache family
// ────────────────────────────────────────────────────────
// ContentCache family (see include/chronon3d/cache/cache_taxonomy.hpp): the
// key is a (canonical path, byte size, mtime) stamp and the value is the
// SHA-256 of the file bytes, so `same key ⇒ same digest` holds and a mutated
// file can never reuse the previous digest.
//
// This is the ONE place that reads a font file to derive identity.  The
// previous revision hashed the whole font inside the per-glyph lookup, so a
// 20 MB CJK face made every glyph request read 20 MB from disk; identity is
// now resolved once per file version (one stat + one LRU hit afterwards).
class FontIdentityMemo final {
public:
    static constexpr std::size_t kMaxEntries = 256;

    [[nodiscard]] assets::ContentDigest digest_for(
        const std::filesystem::path& resolved) {
        std::error_code size_ec;
        const auto byte_size = std::filesystem::file_size(resolved, size_ec);
        std::error_code stamp_ec;
        const auto stamp = std::filesystem::last_write_time(resolved, stamp_ec);
        std::string key = resolved.generic_string();
        key += '\n';
        key += size_ec ? std::string{"-"} : std::to_string(byte_size);
        key += '\n';
        key += stamp_ec ? std::string{"-"}
                        : std::to_string(stamp.time_since_epoch().count());
        if (const auto hit = entries_.get(key)) return *hit;
        // An unreadable file still needs a deterministic identity so the caller
        // fails closed on the same key instead of thrashing the LRU.
        const auto hashed = assets::sha256_file(resolved);
        const assets::ContentDigest digest =
            hashed ? *hashed : assets::sha256_string(resolved.generic_string());
        entries_.put(key, digest);
        return digest;
    }

private:
    cache::LruCache<std::string, assets::ContentDigest> entries_{
        kMaxEntries, 8, cache::CapacityMode::Count};
};

FontIdentityMemo& font_identity_memo() {
    static FontIdentityMemo memo;
    return memo;
}

class PreparationCache final {
public:
    PreparationCache()
        : glyphs_(64u * 1024u * 1024u, 16u, cache::CapacityMode::Weight) {}

    std::shared_ptr<const MtsdfGlyphBitmap> get(
        const assets::AssetResolver& resolver,
        std::string_view font_path,
        std::uint32_t glyph_id,
        const MtsdfProfile& profile,
        std::string_view variation_axes) {
        const auto resolved = resolver.resolve_lexical(std::string(font_path));
        if (!resolved) return nullptr;
        // Content-addressed: the identity is the canonical prepared-font
        // content digest, so the same path with different bytes can never
        // satisfy a hit with a glyph built from the previous bytes.
        const assets::ContentDigest font_digest =
            font_identity_memo().digest_for(*resolved);
        GlyphKey key{font_digest, glyph_id, std::string(variation_axes), profile};
        const std::string resolved_str = resolved->generic_string();
        const std::string axes(variation_axes);
        return glyphs_.compute_if_absent(key, [&, resolved_str, font_digest, axes]() {
#ifdef CHRONON3D_ENABLE_TEXT
            const auto library = shared_library();
            if (!library) return std::pair<std::shared_ptr<const MtsdfGlyphBitmap>, std::size_t>{nullptr, 1};
            auto face = get_face(font_digest, resolved_str, profile.em_px, library, axes);
            if (!face) return std::pair<std::shared_ptr<const MtsdfGlyphBitmap>, std::size_t>{nullptr, 1};
            const auto generated = generate_mtsdf_glyph(
                MtsdfFaceGuard{face->face, &face->mutex}, glyph_id,
                profile.em_px, profile.distance_range_px, profile.padding_px);
            if (!generated || generated->rgba8.empty()) {
                return std::pair<std::shared_ptr<const MtsdfGlyphBitmap>, std::size_t>{nullptr, 1};
            }
            auto payload = std::make_shared<MtsdfGlyphBitmap>(*generated);
            const auto weight = sizeof(MtsdfGlyphBitmap) +
                payload->rgba8.size() * sizeof(float);
            return std::pair<std::shared_ptr<const MtsdfGlyphBitmap>, std::size_t>{
                std::move(payload), weight};
#else
            return std::pair<std::shared_ptr<const MtsdfGlyphBitmap>, std::size_t>{nullptr, 1};
#endif
        });
    }

private:
#ifdef CHRONON3D_ENABLE_TEXT
    // Bounded face residency: daemon long-lived + 200 channels × CJK/arabic
    // would otherwise keep every FT_Face forever.  LRU via LruCache so glyph
    // generation and face lookup share bounded semantics.
    //
    // The key carries the font content digest: without it a face opened from
    // the OLD bytes would stay resident for the same path and happily generate
    // glyphs that the caller then stores under the NEW content digest.
    static constexpr std::size_t kMaxFaces = 32;
    std::shared_ptr<FaceEntry> get_face(
        const assets::ContentDigest& font_digest, const std::string& path,
        std::uint16_t em_px, FT_Library library, const std::string& axes) {
        const auto key = path + "\n" + font_digest.hex() + "\n" +
            std::to_string(em_px) + "\n" + axes;
        return faces_.compute_if_absent(key, [&]() -> std::pair<std::shared_ptr<FaceEntry>, std::size_t> {
            auto face = std::make_shared<FaceEntry>();
            if (FT_New_Face(library, path.c_str(), 0, &face->face) != 0 ||
                !face->face || !apply_face_variations(face->face, library, axes) ||
                FT_Set_Pixel_Sizes(face->face, em_px, em_px) != 0) {
                if (face->face) FT_Done_Face(face->face);
                face->face = nullptr;
                return {nullptr, 1};
            }
            return {face, 1};
        });
    }

    // LruCache is already sharded/thread-safe; weight 1 per face, 32 faces.
    cache::LruCache<std::string, std::shared_ptr<FaceEntry>> faces_{32, 8, cache::CapacityMode::Count};
#endif
    cache::LruCache<GlyphKey, std::shared_ptr<const MtsdfGlyphBitmap>, GlyphKeyHash> glyphs_;
};

PreparationCache& cache_instance() {
    static PreparationCache cache;
    return cache;
}

} // namespace

std::optional<assets::ContentDigest> prepared_font_content_digest(
    const assets::AssetResolver& resolver, std::string_view font_path) {
    if (font_path.empty()) return std::nullopt;
    const auto resolved = resolver.resolve_lexical(std::string(font_path));
    if (!resolved) return std::nullopt;
    return font_identity_memo().digest_for(*resolved);
}

std::shared_ptr<const MtsdfGlyphBitmap> prepare_mtsdf_glyph(
    const assets::AssetResolver& resolver,
    std::string_view font_path,
    std::uint32_t glyph_id,
    const MtsdfProfile& profile) {
    return prepare_mtsdf_glyph(resolver, font_path, glyph_id, profile, {});
}

std::shared_ptr<const MtsdfGlyphBitmap> prepare_mtsdf_glyph(
    const assets::AssetResolver& resolver,
    std::string_view font_path,
    std::uint32_t glyph_id,
    const MtsdfProfile& profile,
    std::string_view variation_axes) {
    if (font_path.empty() || glyph_id == 0) return nullptr;
    return cache_instance().get(resolver, font_path, glyph_id, profile, variation_axes);
}

} // namespace chronon3d::text
