#include <chronon3d/cache/persistent_framebuffer_store.hpp>

namespace chronon3d::cache {

void PersistentFramebufferStore::set_cache_dir(const std::filesystem::path& path) {
    m_cache_dir = path;
}

std::filesystem::path PersistentFramebufferStore::cache_dir() const {
    return m_cache_dir;
}

void PersistentFramebufferStore::set_disabled(bool disabled) {
    m_disabled = disabled;
}

bool PersistentFramebufferStore::is_enabled() const noexcept {
    return !m_disabled;
}


} // namespace chronon3d::cache
