#include <chronon3d/backends/assets/font_cache.hpp>
#include <fstream>

namespace chronon3d {

const CachedFont* FontCache::get_or_load(const std::string& path) {
    auto it = m_cache.find(path);
    if (it != m_cache.end()) {
        return it->second.valid() ? &it->second : nullptr;
    }

    std::ifstream file(path, std::ios::binary);
    CachedFont entry;

    if (file) {
        file.seekg(0, std::ios::end);
        const auto sz = file.tellg();
        if (sz > 0) {
            file.seekg(0, std::ios::beg);
            entry.data.resize(static_cast<size_t>(sz));
            file.read(reinterpret_cast<char*>(entry.data.data()), sz);
        }
    }

    // Insert even if invalid — prevents repeated disk hits on missing fonts.
    auto& stored = m_cache.emplace(path, std::move(entry)).first->second;
    return stored.valid() ? &stored : nullptr;
}

void FontCache::clear() {
    m_cache.clear();
}

} // namespace chronon3d
