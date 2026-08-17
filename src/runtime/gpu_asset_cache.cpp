#include <chronon3d/runtime/gpu_asset_cache.hpp>
#include <chronon3d/core/profiling/profiling.hpp>

#include <algorithm>
#include <iterator>

namespace chronon3d::runtime {

GpuAssetCache::~GpuAssetCache() {
    clear();
}

void GpuAssetCache::attach(RenderSurfaceRegistry& registry,
                           graph::RenderBackend& backend) noexcept {
    std::lock_guard lock(m_mutex);
    m_registry = &registry;
    m_backend = &backend;
}

GpuAssetAcquireResult GpuAssetCache::acquire(
    const GpuAssetKey& key, SurfaceDesc desc, std::span<const float> rgba) {
    std::lock_guard lock(m_mutex);
    if (!m_registry || !m_backend) {
        return {kInvalidRenderSurfaceHandle, false, "GPU asset cache is not attached"};
    }
    if (key.format == PixelFormat::Unknown || key.width == 0 || key.height == 0 ||
        key.width != desc.width || key.height != desc.height || desc.format != key.format) {
        return {kInvalidRenderSurfaceHandle, false, "GPU asset key and surface description disagree"};
    }
    if (desc.bytes == 0) {
        desc.bytes = static_cast<std::size_t>(desc.width) * desc.height * sizeof(float) * 4;
    }
    if (rgba.size_bytes() != desc.bytes) {
        return {kInvalidRenderSurfaceHandle, false, "GPU asset upload size does not match surface"};
    }
    if (m_budget_bytes != 0 && desc.bytes > m_budget_bytes) {
        return {kInvalidRenderSurfaceHandle, false, "GPU asset exceeds cache budget"};
    }

    if (const auto it = m_entries.find(key); it != m_entries.end()) {
        m_lru.splice(m_lru.end(), m_lru, it->second.lru_position);
        it->second.lru_position = std::prev(m_lru.end());
        ++m_stats.hits;
        if (profiling::g_current_counters) {
            profiling::g_current_counters->gpu_asset_cache_hits.fetch_add(1, std::memory_order_relaxed);
        }
        return {it->second.handle, true, {}};
    }
    ++m_stats.misses;
    if (profiling::g_current_counters) {
        profiling::g_current_counters->gpu_asset_cache_misses.fetch_add(1, std::memory_order_relaxed);
    }

    const auto handle = m_registry->create(desc);
    if (handle == kInvalidRenderSurfaceHandle) {
        return {handle, false, "GPU asset surface allocation failed"};
    }
    const auto created = m_backend->create_surface(handle, desc);
    const auto uploaded = created.ok() ? m_backend->upload_surface(handle, desc, rgba) : created;
    if (!uploaded.ok()) {
        (void)m_backend->release_surface(handle);
        (void)m_registry->release(handle);
        return {kInvalidRenderSurfaceHandle, false, uploaded.error().message};
    }

    m_lru.push_back(key);
    m_entries.emplace(key, Entry{handle, desc.bytes, std::prev(m_lru.end())});
    m_resident_bytes += desc.bytes;
    m_stats.resident_bytes = m_resident_bytes;
    m_stats.upload_bytes += desc.bytes;
    evict_to_budget_locked();
    return {handle, false, {}};
}

void GpuAssetCache::evict_to_budget_locked() {
    while (m_budget_bytes != 0 && m_resident_bytes > m_budget_bytes && !m_lru.empty()) {
        const auto key = m_lru.front();
        m_lru.pop_front();
        const auto it = m_entries.find(key);
        if (it == m_entries.end()) continue;
        release_handle(it->second.handle);
        m_resident_bytes -= it->second.bytes;
        m_entries.erase(it);
        ++m_stats.evictions;
    }
    m_stats.resident_bytes = m_resident_bytes;
}

void GpuAssetCache::release_handle(RenderSurfaceHandle handle) noexcept {
    if (m_backend) (void)m_backend->release_surface(handle);
    if (m_registry) (void)m_registry->release(handle);
}

void GpuAssetCache::clear() noexcept {
    std::lock_guard lock(m_mutex);
    for (const auto& [key, entry] : m_entries) {
        (void)key;
        release_handle(entry.handle);
    }
    m_entries.clear();
    m_lru.clear();
    m_resident_bytes = 0;
    m_stats.resident_bytes = 0;
}

void GpuAssetCache::set_budget_bytes(std::size_t budget_bytes) noexcept {
    std::lock_guard lock(m_mutex);
    m_budget_bytes = budget_bytes;
    evict_to_budget_locked();
}

GpuAssetCacheStats GpuAssetCache::stats() const noexcept {
    std::lock_guard lock(m_mutex);
    return m_stats;
}

} // namespace chronon3d::runtime
