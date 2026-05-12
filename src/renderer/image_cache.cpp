#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <chronon3d/renderer/image_cache.hpp>

namespace chronon3d {

const CachedImage* ImageCache::get_or_load(const std::string& path) {
    auto it = m_cache.find(path);
    if (it != m_cache.end()) {
        return it->second.valid() ? &it->second : nullptr;
    }

    CachedImage entry;
    int channels = 0;
    unsigned char* raw = stbi_load(path.c_str(), &entry.width, &entry.height, &channels, 4);

    if (!raw || entry.width <= 0 || entry.height <= 0) {
        // Insert invalid sentinel so we don't retry every frame.
        m_cache.emplace(path, CachedImage{});
        return nullptr;
    }

    entry.pixels = std::unique_ptr<unsigned char[], void(*)(void*)>(raw, stbi_image_free);
    auto& stored = m_cache.emplace(path, std::move(entry)).first->second;
    return &stored;
}

void ImageCache::clear() {
    m_cache.clear();
}

} // namespace chronon3d
