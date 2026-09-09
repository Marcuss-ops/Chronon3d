#include <chronon3d/runtime/gpu_asset_cache.hpp>
#include <chronon3d/core/profiling/profiling.hpp>

#include <algorithm>
#include <iterator>
#include <vector>

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

bool GpuAssetCache::release_backend_surface(RenderSurfaceHandle handle) noexcept {
    if (handle == kInvalidRenderSurfaceHandle || !m_backend) {
        return true;
    }
    const auto result = m_backend->release_surface(handle);
    // A backend without native surface storage (UnsupportedCapability) never
    // owned the allocation, so the entry's residency accounting is released.
    // AUDIT-2026-09-09 E-03 — the case is now counted (stats().
    // unsupported_capability_releases) so the logical-only release stays
    // observable instead of silently blending into real release failures.
    if (!result.ok() &&
        result.error().code == graph::RenderBackendErrorCode::UnsupportedCapability) {
        m_unsupported_capability_releases.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    return result.ok();
}

void GpuAssetCache::release_and_account(
    RenderSurfaceHandle handle, std::size_t bytes, bool physical_already_gone) {
    // The caller has already removed the entry from m_entries/m_lru and must
    // NOT hold m_mutex here: the backend release below is a device call.
    const bool freed = physical_already_gone || release_backend_surface(handle);

    std::lock_guard lock(m_mutex);
    if (freed) {
        m_resident_bytes = (m_resident_bytes >= bytes) ? (m_resident_bytes - bytes) : 0;
        ++m_stats.evictions;
        m_stats.evicted_bytes += bytes;
        if (profiling::g_current_counters) {
            auto* counters = profiling::g_current_counters;
            counters->gpu_asset_cache_evictions.fetch_add(1, std::memory_order_relaxed);
            counters->gpu_asset_cache_evicted_bytes.fetch_add(
                bytes, std::memory_order_relaxed);
        }
    } else {
        // The backend could not free the surface.  Keep the bytes charged to
        // resident accounting (they are still owned by the device) and record
        // the failure so the divergence is observable instead of silent.
        ++m_stats.release_failures;
    }
    m_stats.resident_bytes = m_resident_bytes;
    if (profiling::g_current_counters) {
        profiling::g_current_counters->gpu_asset_cache_resident_bytes.store(
            m_resident_bytes, std::memory_order_relaxed);
    }
    // Always drop the logical handle: the cache must not hand out an entry it
    // no longer tracks.  On a backend release failure the device allocation is
    // orphaned but reported through release_failures.
    if (m_registry) {
        (void)m_registry->release(handle);
    }
}

void GpuAssetCache::evict_to_budget() {
    for (;;) {
        GpuAssetKey key{};
        RenderSurfaceHandle handle{kInvalidRenderSurfaceHandle};
        std::size_t bytes{0};
        {
            std::lock_guard lock(m_mutex);
            if (m_budget_bytes == 0 || m_resident_bytes <= m_budget_bytes ||
                m_lru.empty()) {
                break;
            }
            key = m_lru.front();
            m_lru.pop_front();
            const auto it = m_entries.find(key);
            if (it == m_entries.end()) {
                continue;
            }
            handle = it->second.handle;
            bytes = it->second.bytes;
            m_entries.erase(it);
        }

        // Reserve the key while the device release runs so a concurrent
        // acquire of the same key waits for the old surface to be reclaimed
        // before uploading a replacement.
        {
            std::lock_guard lock(m_mutex);
            m_inflight.emplace(key, InflightUpload{true, handle});
        }
        release_and_account(handle, bytes, false);
        {
            std::lock_guard lock(m_mutex);
            m_inflight.erase(key);
        }
        m_inflight_cv.notify_all();
    }
}

GpuAssetAcquireResult GpuAssetCache::acquire(
    const GpuAssetKey& key, SurfaceDesc desc, std::span<const float> rgba) {
    std::unique_lock lock(m_mutex);
    if (!m_registry || !m_backend) {
        return {kInvalidRenderSurfaceHandle, false, "GPU asset cache is not attached"};
    }
    if (key.format == PixelFormat::Unknown || key.width == 0 || key.height == 0 ||
        key.width != desc.width || key.height != desc.height || desc.format != key.format) {
        return {kInvalidRenderSurfaceHandle, false, "GPU asset key and surface description disagree"};
    }
    if (desc.bytes == 0) {
        desc.bytes = tight_surface_bytes(desc.format, desc.width, desc.height);
    }
    if (rgba.size_bytes() != desc.bytes) {
        return {kInvalidRenderSurfaceHandle, false, "GPU asset upload size does not match surface"};
    }
    // Static assets are deliberately job-persistent: the logical handle and
    // its device allocation survive frame-local surface cleanup and can be
    // reused by every frame (and every job sharing this runtime cache).
    desc.lifetime = LifetimeClass::JobPersistent;
    if (m_budget_bytes != 0 && desc.bytes > m_budget_bytes) {
        return {kInvalidRenderSurfaceHandle, false, "GPU asset exceeds cache budget"};
    }

    for (;;) {
        if (const auto it = m_entries.find(key); it != m_entries.end()) {
            // The registry and backend have separate ownership.  A transient
            // cleanup or pooled framebuffer release can invalidate the
            // physical backend surface while the logical cache entry is still
            // present.  Never return that stale handle: evict it (including
            // the registry entry, so no logical handle is orphaned) and
            // rebuild the asset.
            if (!m_registry->lookup(it->second.handle) ||
                !m_backend->is_native_surface_valid(it->second.handle)) {
                // The registry and backend may disagree about the handle:
                //  - registry alive + backend dead  → physical memory is gone,
                //    only the logical registry entry must be dropped;
                //  - registry dead + backend alive   → reclaim via the device.
                // Either way release_and_account() fixes the accounting and
                // never leaves an orphaned registry handle behind.
                const bool physical_already_gone =
                    !m_backend->is_native_surface_valid(it->second.handle);
                const RenderSurfaceHandle stale_handle = it->second.handle;
                const std::size_t stale_bytes = it->second.bytes;
                m_lru.erase(it->second.lru_position);
                m_entries.erase(it);
                lock.unlock();
                release_and_account(stale_handle, stale_bytes, physical_already_gone);
                lock.lock();
                continue;  // fall through to (re)build the asset below
            }
            m_lru.splice(m_lru.end(), m_lru, it->second.lru_position);
            it->second.lru_position = std::prev(m_lru.end());
            ++m_stats.hits;
            if (profiling::g_current_counters) {
                auto* counters = profiling::g_current_counters;
                counters->gpu_asset_cache_hits.fetch_add(1, std::memory_order_relaxed);
            }
            return {it->second.handle, true, {}};
        }

        // Another thread is populating or releasing this key: wait for it so
        // one logical surface per key is always guaranteed.
        if (m_inflight.find(key) != m_inflight.end()) {
            m_inflight_cv.wait(lock, [&] {
                return m_inflight.find(key) == m_inflight.end();
            });
            continue;
        }
        break;
    }

    ++m_stats.misses;
    if (profiling::g_current_counters) {
        profiling::g_current_counters->gpu_asset_cache_misses.fetch_add(1, std::memory_order_relaxed);
    }

    const auto handle = m_registry->create(desc);
    if (handle == kInvalidRenderSurfaceHandle) {
        return {handle, false, "GPU asset surface allocation failed"};
    }
    m_inflight.emplace(key, InflightUpload{false, handle});
    lock.unlock();

    // ── Device work runs outside the cache mutex ───────────────────────────
    // Every backend serializes its own surface API internally, so parallel
    // uploads of different assets never corrupt device state, and slow device
    // calls no longer stall cache lookups for unrelated keys.
    auto created = m_backend->create_surface(handle, desc);
    profiling::GpuUploadProducerScope upload_scope(profiling::GpuUploadProducer::Image);
    const auto uploaded = created.ok()
        ? m_backend->upload_surface(handle, desc, rgba)
        : created;
    const bool upload_ok = uploaded.ok();

    // ── Finalize under the lock ────────────────────────────────────────────
    lock.lock();
    if (upload_ok) {
        m_lru.push_back(key);
        m_entries.emplace(key, Entry{handle, desc.bytes, std::prev(m_lru.end())});
        m_resident_bytes += desc.bytes;
        m_stats.resident_bytes = m_resident_bytes;
        if (profiling::g_current_counters) {
            profiling::g_current_counters->gpu_asset_cache_resident_bytes.store(
                m_resident_bytes, std::memory_order_relaxed);
        }
        m_stats.upload_bytes += desc.bytes;
        ++m_stats.initial_uploads;
        m_stats.initial_upload_bytes += desc.bytes;
        if (profiling::g_current_counters) {
            auto* counters = profiling::g_current_counters;
            counters->gpu_asset_cache_initial_uploads.fetch_add(1, std::memory_order_relaxed);
            counters->gpu_asset_cache_initial_upload_bytes.fetch_add(
                desc.bytes, std::memory_order_relaxed);
        }
    }
    m_inflight.erase(key);
    lock.unlock();
    m_inflight_cv.notify_all();

    if (!upload_ok) {
        // Reclaim the failed surface: device release outside the lock, then
        // drop the logical registry entry under the lock.
        (void)m_backend->release_surface(handle);
        std::lock_guard cleanup_lock(m_mutex);
        if (m_registry) {
            (void)m_registry->release(handle);
        }
        return {kInvalidRenderSurfaceHandle, false, uploaded.error().message};
    }

    evict_to_budget();
    return {handle, false, {}};
}

void GpuAssetCache::clear() noexcept {
    decltype(m_entries) entries;
    {
        std::lock_guard lock(m_mutex);
        entries.swap(m_entries);
        m_lru.clear();
        m_resident_bytes = 0;
        m_stats.resident_bytes = 0;
        if (profiling::g_current_counters) {
            profiling::g_current_counters->gpu_asset_cache_resident_bytes.store(
                0, std::memory_order_relaxed);
        }
    }
    // Release device surfaces outside the lock; the cache is being torn down
    // (or detached), so any remaining backend failure is best-effort.
    for (const auto& [key, entry] : entries) {
        (void)key;
        (void)release_backend_surface(entry.handle);
    }
    if (!entries.empty()) {
        std::lock_guard lock(m_mutex);
        for (const auto& [key, entry] : entries) {
            (void)key;
            if (m_registry) {
                (void)m_registry->release(entry.handle);
            }
        }
    }
}

void GpuAssetCache::set_budget_bytes(std::size_t budget_bytes) noexcept {
    {
        std::lock_guard lock(m_mutex);
        m_budget_bytes = budget_bytes;
    }
    evict_to_budget();
}

GpuAssetCacheStats GpuAssetCache::stats() const noexcept {
    std::lock_guard lock(m_mutex);
    GpuAssetCacheStats out = m_stats;
    out.unsupported_capability_releases = static_cast<std::size_t>(
        m_unsupported_capability_releases.load(std::memory_order_relaxed));
    return out;
}

} // namespace chronon3d::runtime
