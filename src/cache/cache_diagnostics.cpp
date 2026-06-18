// =============================================================================
// cache_diagnostics.cpp — Runtime cache diagnostics registry (PR 7).
// =============================================================================

#include <chronon3d/cache/cache_diagnostics.hpp>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace chronon3d::cache {

CacheDiagnostics& CacheDiagnostics::instance() {
    static CacheDiagnostics s_instance;
    return s_instance;
}

// ── Registration ──────────────────────────────────────────────────────────

CacheDiagnostics::Handle CacheDiagnostics::register_cache(
    CacheDomain                       domain,
    std::function<GenericCacheStats()> stats_fn,
    std::function<void()>             clear_fn,
    std::function<CapacityMode()>     mode_fn,
    std::size_t                       capacity)
{
    auto* entry = new Entry{std::move(stats_fn), std::move(clear_fn),
                            std::move(mode_fn), capacity};
    {
        std::lock_guard lock(m_mutex);
        m_entries[domain].push_back(entry);
    }
    return Handle{this, entry};
}

void CacheDiagnostics::unregister(Entry* entry) {
    std::lock_guard lock(m_mutex);
    for (auto& [domain, vec] : m_entries) {
        auto it = std::find(vec.begin(), vec.end(), entry);
        if (it != vec.end()) {
            vec.erase(it);
            delete entry;
            if (vec.empty()) {
                // Don't erase from the map mid-iteration; cleanup happens
                // lazily during snapshot/clear which only reads values.
            }
            return;
        }
    }
    // Entry not found — already unregistered or never registered.
    delete entry;
}

// ── Snapshot ──────────────────────────────────────────────────────────────

std::vector<CacheSnapshot> CacheDiagnostics::snapshot() const {
    std::vector<CacheSnapshot> result;
    if (!m_enabled.load(std::memory_order_relaxed)) return result;

    std::lock_guard lock(m_mutex);
    for (const auto& [domain, vec] : m_entries) {
        for (const auto* entry : vec) {
            GenericCacheStats gs = entry->stats_fn();
            result.push_back(CacheSnapshot{
                .domain         = domain,
                .hits           = gs.hits,
                .misses         = gs.misses,
                .evictions      = gs.evictions,
                .current_size   = gs.current_size,
                .current_weight = gs.current_weight,
                .capacity       = entry->capacity,
                .mode           = entry->mode_fn(),
                .enabled        = true,
            });
        }
    }
    return result;
}

DomainSnapshot CacheDiagnostics::snapshot_by_domain(CacheDomain domain) const {
    DomainSnapshot ds{.domain = domain};
    if (!m_enabled.load(std::memory_order_relaxed)) return ds;

    std::lock_guard lock(m_mutex);
    auto it = m_entries.find(domain);
    if (it == m_entries.end()) return ds;

    ds.instance_count = it->second.size();
    for (const auto* entry : it->second) {
        GenericCacheStats gs = entry->stats_fn();
        ds.hits           += gs.hits;
        ds.misses         += gs.misses;
        ds.evictions      += gs.evictions;
        ds.current_size   += gs.current_size;
        ds.current_weight += gs.current_weight;
        ds.total_capacity += entry->capacity;
    }
    return ds;
}

std::vector<DomainSnapshot> CacheDiagnostics::snapshot_all_domains() const {
    std::vector<DomainSnapshot> result;
    if (!m_enabled.load(std::memory_order_relaxed)) return result;

    std::lock_guard lock(m_mutex);
    for (const auto& [domain, vec] : m_entries) {
        if (vec.empty()) continue;
        DomainSnapshot ds{.domain = domain, .instance_count = vec.size()};
        for (const auto* entry : vec) {
            GenericCacheStats gs = entry->stats_fn();
            ds.hits           += gs.hits;
            ds.misses         += gs.misses;
            ds.evictions      += gs.evictions;
            ds.current_size   += gs.current_size;
            ds.current_weight += gs.current_weight;
            ds.total_capacity += entry->capacity;
        }
        result.push_back(ds);
    }
    return result;
}

// ── Clear ─────────────────────────────────────────────────────────────────

std::size_t CacheDiagnostics::clear_by_domain(CacheDomain domain) {
    if (!m_enabled.load(std::memory_order_relaxed)) return 0;

    std::lock_guard lock(m_mutex);
    auto it = m_entries.find(domain);
    if (it == m_entries.end()) return 0;

    std::size_t count = it->second.size();
    for (auto* entry : it->second) {
        entry->clear_fn();
    }
    return count;
}

std::size_t CacheDiagnostics::clear_all() {
    if (!m_enabled.load(std::memory_order_relaxed)) return 0;

    std::lock_guard lock(m_mutex);
    std::size_t total = 0;
    for (auto& [domain, vec] : m_entries) {
        total += vec.size();
        for (auto* entry : vec) {
            entry->clear_fn();
        }
    }
    return total;
}

// ── Introspection ─────────────────────────────────────────────────────────

std::size_t CacheDiagnostics::registered_count() const {
    std::lock_guard lock(m_mutex);
    std::size_t total = 0;
    for (const auto& [domain, vec] : m_entries) {
        total += vec.size();
    }
    return total;
}

std::size_t CacheDiagnostics::registered_count(CacheDomain domain) const {
    std::lock_guard lock(m_mutex);
    auto it = m_entries.find(domain);
    return it != m_entries.end() ? it->second.size() : 0;
}

// ── Toggle ────────────────────────────────────────────────────────────────

void CacheDiagnostics::set_enabled(bool enabled) noexcept {
    m_enabled.store(enabled, std::memory_order_relaxed);
}

bool CacheDiagnostics::is_enabled() const noexcept {
    return m_enabled.load(std::memory_order_relaxed);
}

} // namespace chronon3d::cache
