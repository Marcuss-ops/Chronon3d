#pragma once

// Focused font/cache surface for text code that does not need the
// TextRenderResources aggregator or its raster/glyph-atlas caches.

#include <blend2d.h>
#include <blend2d/font.h>
#include <blend2d/path.h>

#include <chronon3d/core/types/types.hpp>

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#ifdef CHRONON3D_ENABLE_TEXT
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_OUTLINE_H
#endif

namespace chronon3d {

namespace assets { class AssetResolver; }

/// Stable, allocation-free identity used by the per-renderer glyph atlas.
inline std::uint64_t glyph_font_id(std::string_view font_path) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char c : font_path) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return hash;
}

// BLFontFaceCache — thread-safe cache of BLFontFace objects.
class BLFontFaceCache {
public:
    explicit BLFontFaceCache(std::atomic<bool>* fence = nullptr) noexcept
        : fence_(fence) {}

    void set_fence(std::atomic<bool>* fence) noexcept { fence_ = fence; }
    [[nodiscard]] const BLFontFace* get_face(const std::string& resolved_path);
    [[nodiscard]] bool contains(const std::string& resolved_path) const;

private:
    std::atomic<bool>* fence_{nullptr};
    std::unordered_map<std::string, BLFontFace> faces_;
    mutable std::mutex mutex_;
};

#ifdef CHRONON3D_ENABLE_TEXT

struct FTFaceKey {
    std::string path;
    int face_index{0};
    int size_bucket{0};

    bool operator==(const FTFaceKey& o) const noexcept {
        return face_index == o.face_index
            && size_bucket == o.size_bucket
            && path == o.path;
    }
};

struct FTFaceKeyHash {
    std::size_t operator()(const FTFaceKey& k) const noexcept {
        std::size_t h = std::hash<std::string>{}(k.path);
        h ^= static_cast<std::size_t>(k.face_index) + 0x9e3779b97f4a7c15ULL
             + (h << 6) + (h >> 2);
        h ^= static_cast<std::size_t>(k.size_bucket) + 0x9e3779b97f4a7c15ULL
             + (h << 6) + (h >> 2);
        return h;
    }
};

struct FreeTypeFaceLease {
    std::shared_ptr<FT_Face> face;
    std::shared_ptr<std::mutex> mutex;

    [[nodiscard]] bool valid() const noexcept { return face != nullptr; }
};

class FreeTypeFaceCache {
public:
    explicit FreeTypeFaceCache(std::atomic<bool>* fence = nullptr) noexcept;
    ~FreeTypeFaceCache();

    FreeTypeFaceCache(const FreeTypeFaceCache&) = delete;
    FreeTypeFaceCache& operator=(const FreeTypeFaceCache&) = delete;

    void set_fence(std::atomic<bool>* fence) noexcept { fence_ = fence; }

#ifndef NDEBUG
    [[nodiscard]] int in_flight() const noexcept {
        return in_flight_.load(std::memory_order_relaxed);
    }
#endif

    [[nodiscard]] std::shared_ptr<FT_Face> get_face(
        const std::string& resolved_path,
        int face_index,
        f32 font_size
    );

    [[nodiscard]] FreeTypeFaceLease get_face_lease(
        const std::string& resolved_path,
        int face_index,
        f32 font_size
    );

private:
    static FT_Library shared_library();

    std::atomic<bool>* fence_{nullptr};
#ifndef NDEBUG
    std::atomic<int> in_flight_{0};
#endif
    std::unordered_map<FTFaceKey, FreeTypeFaceLease, FTFaceKeyHash> faces_;
    std::mutex mutex_;
};

class GlyphOutlineBuilder {
public:
    GlyphOutlineBuilder() = default;

    [[nodiscard]] BLPath build_path(
        FT_Face ft_face,
        u32 glyph_id,
        float origin_x,
        float origin_y
    );

private:
    std::mutex mutex_;
};

#endif // CHRONON3D_ENABLE_TEXT

struct FontFaceHandle {
    const BLFontFace* bl_face{nullptr};
    f32 font_size{0.0f};

#ifdef CHRONON3D_ENABLE_TEXT
    std::shared_ptr<FT_Face> ft_lifeline;
    FT_Face                  ft_face{nullptr};
    std::shared_ptr<std::mutex> ft_mutex;
#endif

#ifdef CHRONON3D_ENABLE_TEXT
    GlyphOutlineBuilder* outlines{nullptr};
#endif

    [[nodiscard]] bool valid() const noexcept {
        return bl_face != nullptr && !bl_face->empty();
    }
};

} // namespace chronon3d
