#include <chronon3d/backends/assets/image_cache.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d {

const CachedImage* ImageCache::get_or_load(const std::string& path) {
    auto it = m_cache.find(path);
    if (it != m_cache.end()) {
        return it->second.valid() ? &it->second : nullptr;
    }

    if (!m_backend) {
        spdlog::error("ImageCache: cannot load '{}' - no backend set", path);
        return nullptr;
    }

    auto buffer = m_backend->load_image(path);
    if (!buffer || !buffer->pixels) {
        // Insert invalid sentinel so we don't retry every frame.
        m_cache.emplace(path, CachedImage{});
        return nullptr;
    }

    CachedImage entry;
    entry.width = buffer->width;
    entry.height = buffer->height;
    entry.pixels = std::move(buffer->pixels);

    auto& stored = m_cache.emplace(path, std::move(entry)).first->second;
    return &stored;
}

void ImageCache::clear() {
    m_cache.clear();
}

} // namespace chronon3d
