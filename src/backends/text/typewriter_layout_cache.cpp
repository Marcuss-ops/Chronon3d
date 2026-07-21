#include <chronon3d/text/typewriter_layout_cache.hpp>

#include <chronon3d/cache/lru_cache.hpp>

namespace chronon3d::content::text {

class TypewriterLayoutCache::Impl {
public:
    Impl()
        : cache_(64, 2, cache::CapacityMode::Count) {}

    cache::LruCache<
        TypewriterLayoutKey,
        std::shared_ptr<const TypewriterLayoutEntry>> cache_;
};

TypewriterLayoutCache::TypewriterLayoutCache()
    : m_impl(std::make_unique<Impl>()) {}

TypewriterLayoutCache::~TypewriterLayoutCache() = default;

TypewriterLayoutCache::TypewriterLayoutCache(TypewriterLayoutCache&&) noexcept = default;
TypewriterLayoutCache& TypewriterLayoutCache::operator=(TypewriterLayoutCache&&) noexcept = default;

std::shared_ptr<const TypewriterLayoutEntry> TypewriterLayoutCache::get(const TypewriterLayoutKey& key) {
    auto val = m_impl->cache_.get(key);
    if (val) return *val;
    return nullptr;
}

void TypewriterLayoutCache::put(const TypewriterLayoutKey& key, std::shared_ptr<const TypewriterLayoutEntry> entry) {
    m_impl->cache_.put(key, std::move(entry), 1);
}

void TypewriterLayoutCache::clear() {
    m_impl->cache_.clear();
}

} // namespace chronon3d::content::text
