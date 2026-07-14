// =============================================================================
// cache_diagnostics.cpp — Runtime cache diagnostics registry.
// =============================================================================

#include <chronon3d/cache/cache_diagnostics.hpp>
#include <algorithm>
#include <shared_mutex>  // TICKET-lock-free-shared_mutex — std::shared_lock lives here.
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
        std::unique_lock lock(m_mutex);
        m_entries[domain].insert(entry);
        // TICKET-O(n)-audit — unordered_set::insert silently dedups.
        // Duplicate registrations of the same Entry* pointer are now
        // idempotent and ignored (the previous vector::push_back would
        // have surfaced them in snapshot(); callers must verify they
        // register each Entry exactly once).
    }
    return Handle{this, entry};
}

void CacheDiagnostics::unregister(Entry* entry) {
    std::unique_lock lock(m_mutex);
    for (auto& [domain, set] : m_entries) {
        // TICKET-O(n)-audit — O(1) lookup in unordered_set replaces the
        // previous O(n) std::find linear scan.
        auto it = set.find(entry);
        if (it != set.end()) {
            set.erase(it);
            delete entry;
            if (set.empty()) {
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

    std::shared_lock lock(m_mutex);
    for (const auto& [domain, set] : m_entries) {
        for (const auto* entry : set) {
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

    std::shared_lock lock(m_mutex);
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

    std::shared_lock lock(m_mutex);
    for (const auto& [domain, set] : m_entries) {
        if (set.empty()) continue;
        DomainSnapshot ds{.domain = domain, .instance_count = set.size()};
        for (const auto* entry : set) {
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

    // P1-12 (lock-ordering): copy each `clear_fn` (a `std::function<void()>`
    // owning its own captured state) under `m_mutex`, then release the lock
    // BEFORE invoking the callbacks.  The previous implementation invoked the
    // callbacks while still holding the exclusive lock, which could deadlock
    // against any `clear_fn` that (a) takes other locks, (b) unregisters a
    // cache (via the same `m_mutex`), or (c) re-enters the diagnostics
    // (snapshot / register / another clear_*). Copying the `std::function`
    // decouples our local copy from the registry `Entry` lifetime (the
    // `clear_fn` inside the Entry can be destroyed by a concurrent
    // `unregister()` without affecting our copy — the lambda body still
    // holds the original captured state).
    std::vector<std::function<void()>> to_invoke;
    {
        std::unique_lock lock(m_mutex);
        auto it = m_entries.find(domain);
        if (it == m_entries.end()) return 0;
        to_invoke.reserve(it->second.size());
        for (const Entry* entry : it->second) {
            to_invoke.push_back(entry->clear_fn);
        }
    }
    for (auto& fn : to_invoke) fn();
    return to_invoke.size();
}

std::size_t CacheDiagnostics::clear_all() {
    if (!m_enabled.load(std::memory_order_relaxed)) return 0;

    // P1-12 (lock-ordering): see `clear_by_domain()` above. Same pattern:
    // collect callback copies under the lock, release, then invoke.
    std::vector<std::function<void()>> to_invoke;
    {
        std::unique_lock lock(m_mutex);
        for (const auto& [domain, set] : m_entries) {
            to_invoke.reserve(to_invoke.size() + set.size());
            for (const Entry* entry : set) {
                to_invoke.push_back(entry->clear_fn);
            }
        }
    }
    for (auto& fn : to_invoke) fn();
    return to_invoke.size();
}

// ── Introspection ─────────────────────────────────────────────────────────

std::size_t CacheDiagnostics::registered_count() const {
    std::shared_lock lock(m_mutex);
    std::size_t total = 0;
    for (const auto& [domain, set] : m_entries) {
        total += set.size();
    }
    return total;
}

std::size_t CacheDiagnostics::registered_count(CacheDomain domain) const {
    std::shared_lock lock(m_mutex);
    auto it = m_entries.find(domain);
    return it != m_entries.end() ? it->second.size() : 0;
}

// (registered_count(CacheDomain) uses unordered_set::size which is O(1)
 //  instead of std::distance(vec.begin(), vec.end()). TICKET-O(n)-audit.)

// ── Toggle ────────────────────────────────────────────────────────────────

void CacheDiagnostics::set_enabled(bool enabled) noexcept {
    m_enabled.store(enabled, std::memory_order_relaxed);
}

bool CacheDiagnostics::is_enabled() const noexcept {
    return m_enabled.load(std::memory_order_relaxed);
}

} // namespace chronon3d::cache
