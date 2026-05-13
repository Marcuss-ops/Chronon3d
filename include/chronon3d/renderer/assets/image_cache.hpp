#pragma once

#include <chronon3d/core/types.hpp>
#include <string>
#include <unordered_map>
#include <memory>

namespace chronon3d {

// Cached image entry. Non-copyable, movable.
// Pixels are freed via stbi_image_free when the entry is destroyed.
struct CachedImage {
    int   width{0};
    int   height{0};
    std::unique_ptr<unsigned char[], void(*)(void*)> pixels{nullptr, [](void*){}};

    [[nodiscard]] bool valid() const { return pixels != nullptr && width > 0 && height > 0; }
};

// Per-renderer image cache keyed by file path.
// NOT thread-safe — one cache per renderer thread.
// For parallel frame rendering use one SoftwareRenderer per worker thread.
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
