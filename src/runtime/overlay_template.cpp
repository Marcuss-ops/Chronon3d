#include <chronon3d/runtime/overlay_template.hpp>

#include <utility>

namespace chronon3d::runtime {

OverlayTemplateCache::OverlayTemplateCache(std::size_t capacity_entries)
    : m_cache(capacity_entries == 0 ? std::size_t{1} : capacity_entries,
              /*num_shards=*/1,  // single LRU: templates are few and read-mostly
              cache::CapacityMode::Count) {}

CompiledOverlayTemplate OverlayTemplateCache::compile(
    const OverlayTemplateDesc& desc,
    const std::function<CommandPlan(const OverlayTemplateDesc&)>& builder) {
    return m_cache.compute_if_absent(desc, [&]() {
        // CapacityMode::Count ignores the returned weight (each entry = 1).
        return std::make_pair(CompiledOverlayTemplate{desc, builder(desc)}, std::size_t{1});
    });
}

void OverlayTemplateCache::clear() {
    m_cache.clear();
}

std::size_t OverlayTemplateCache::capacity() const {
    return m_cache.capacity();
}

OverlayTemplateCache::Stats OverlayTemplateCache::stats() const {
    const auto s = m_cache.stats();
    return Stats{s.hits, s.misses, s.evictions, s.current_size};
}

} // namespace chronon3d::runtime
