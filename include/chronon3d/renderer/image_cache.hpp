#pragma once

#include <chronon3d/core/types.hpp>
#include <string>
#include <unordered_map>
#include <memory>

// stbi forward-compatible: pixels owned by the cache entry.
struct stbi_cached_image {
    int width{0};
    int height{0};
    unsigned char* pixels{nullptr}; // freed on destruction via stbi_image_free
};

namespace chronon3d {

// Cached image entry. Non-copyable, movable.
struct CachedImage {
    int   width{0};
    int   height{0};
    std::unique_ptr<unsigned char[], void(*)(void*)> pixels{nullptr, [](void*){}};

    [[nodiscard]] bool valid() const { return pixels != nullptr && width > 0 && height > 0; }
};

// Thread-safe: NOT — single-threaded use only (one renderer per thread).
// Keyed by absolute or relative file path.
class ImageCache {
public:
    // Returns nullptr if the image cannot be loaded.
    const CachedImage* get_or_load(const std::string& path);
    void clear();
    [[nodiscard]] usize size() const { return m_cache.size(); }

private:
    std::unordered_map<std::string, CachedImage> m_cache;
};

} // namespace chronon3d
