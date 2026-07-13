#include <chronon3d/text/typewriter_layout_cache.hpp>

#include <chronon3d/cache/lru_cache.hpp>

namespace chronon3d::content::text {

class TypewriterLayoutCache::Impl {
public:
    Impl()
        : cache(64, 2, chronon3d::cache::CapacityMode::Count) {}

    chronon3d::cache::LruCache<
        TypewriterLayoutKey,
        std::shared_ptr<const TypewriterLayoutEntry>> cache;
};

TypewriterLayoutCache::TypewriterLayoutCache()
    : m_impl(std::make_unique<Impl>()) {}

TypewriterLayoutCache::~TypewriterLayoutCache() = default;

TypewriterLayoutCache::TypewriterLayoutCache(TypewriterLayoutCache&&) noexcept = default;
TypewriterLayoutCache& TypewriterLayoutCache::operator=(TypewriterLayoutCache&&) noexcept = default;

std::shared_ptr<const TypewriterLayoutEntry> TypewriterLayoutCache::get(const TypewriterLayoutKey& key) {
    auto val = m_impl->cache.get(key);
    if (val) return *val;
    return nullptr;
}

void TypewriterLayoutCache::put(const TypewriterLayoutKey& key, std::shared_ptr<const TypewriterLayoutEntry> entry) {
    m_impl->cache.put(key, std::move(entry), 1);
}

void TypewriterLayoutCache::clear() {
    m_impl->cache.clear();
}

} // namespace chronon3d::content::text
