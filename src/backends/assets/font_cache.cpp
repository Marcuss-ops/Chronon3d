#include <chronon3d/backends/assets/font_cache.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d {

const CachedFont* FontCache::get_or_load(const std::string& path) {
    auto it = m_cache.find(path);
    if (it != m_cache.end()) {
        return it->second.valid() ? &it->second : nullptr;
    }

    if (!m_backend) {
        spdlog::error("FontCache: cannot load '{}' - no backend set", path);
        return nullptr;
    }

    if (!m_backend->load_font(path)) {
        // Insert invalid sentinel.
        m_cache.emplace(path, CachedFont{});
        return nullptr;
    }

    CachedFont entry;
    entry.path = path;
    auto& stored = m_cache.emplace(path, std::move(entry)).first->second;
    return &stored;
}

void FontCache::clear() {
    m_cache.clear();
}

} // namespace chronon3d
