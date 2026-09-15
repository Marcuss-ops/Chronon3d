#include <chronon3d/text/mtsdf_preparation.hpp>

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/backends/text/mtsdf_generator.hpp>
#include <chronon3d/cache/lru_cache.hpp>

#ifdef CHRONON3D_ENABLE_TEXT
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

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

struct GlyphKey {
    std::string font_path;
    std::uint32_t glyph_id{0};
    MtsdfProfile profile{};

    friend bool operator==(const GlyphKey&, const GlyphKey&) = default;
};

struct GlyphKeyHash {
    std::size_t operator()(const GlyphKey& key) const noexcept {
        std::size_t value = std::hash<std::string>{}(key.font_path);
        value ^= static_cast<std::size_t>(key.glyph_id) +
            0x9e3779b97f4a7c15ULL + (value << 6U) + (value >> 2U);
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

class PreparationCache final {
public:
    PreparationCache()
        : glyphs_(64u * 1024u * 1024u, 16u, cache::CapacityMode::Weight) {}

    std::shared_ptr<const MtsdfGlyphBitmap> get(
        const assets::AssetResolver& resolver,
        std::string_view font_path,
        std::uint32_t glyph_id,
        const MtsdfProfile& profile) {
        const auto resolved = resolver.resolve_lexical(std::string(font_path));
        if (!resolved) return nullptr;
        GlyphKey key{resolved->generic_string(), glyph_id, profile};
        return glyphs_.compute_if_absent(key, [&]() {
#ifdef CHRONON3D_ENABLE_TEXT
            const auto library = shared_library();
            if (!library) return std::pair<std::shared_ptr<const MtsdfGlyphBitmap>, std::size_t>{nullptr, 1};
            auto face = get_face(key.font_path, profile.em_px, library);
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
    std::shared_ptr<FaceEntry> get_face(
        const std::string& path, std::uint16_t em_px, FT_Library library) {
        std::lock_guard lock(face_mutex_);
        const auto key = path + "\n" + std::to_string(em_px);
        if (const auto found = faces_.find(key); found != faces_.end()) {
            return found->second;
        }
        auto face = std::make_shared<FaceEntry>();
        if (FT_New_Face(library, path.c_str(), 0, &face->face) != 0 ||
            !face->face || FT_Set_Pixel_Sizes(face->face, em_px, em_px) != 0) {
            if (face->face) FT_Done_Face(face->face);
            face->face = nullptr;
            return nullptr;
        }
        faces_.emplace(key, face);
        return face;
    }

    std::mutex face_mutex_;
    std::unordered_map<std::string, std::shared_ptr<FaceEntry>> faces_;
#endif
    cache::LruCache<GlyphKey, std::shared_ptr<const MtsdfGlyphBitmap>, GlyphKeyHash> glyphs_;
};

PreparationCache& cache_instance() {
    static PreparationCache cache;
    return cache;
}

} // namespace

std::shared_ptr<const MtsdfGlyphBitmap> prepare_mtsdf_glyph(
    const assets::AssetResolver& resolver,
    std::string_view font_path,
    std::uint32_t glyph_id,
    const MtsdfProfile& profile) {
    if (font_path.empty() || glyph_id == 0) return nullptr;
    return cache_instance().get(resolver, font_path, glyph_id, profile);
}

} // namespace chronon3d::text
