// =============================================================================
// cache_diagnostics.cpp — Runtime cache diagnostics registry.
// =============================================================================

#include <chronon3d/cache/cache_diagnostics.hpp>
#include <algorithm>
#include <shared_mutex>  // TICKET-lock-free-shared_mutex — std::shared_lock lives here.
#include <spdlog/spdlog.h>

namespace chronon3d::cache {

// P1-10 — `CacheDiagnostics::instance()` REMOVED.  Per-runtime
// diagnostics are owned by `RenderRuntime::m_diagnostics` and accessed
// via `runtime.diagnostics()`.  External callers (CLI, tests) construct
// their own local `CacheDiagnostics` and pass a pointer to the caches.

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
    return Handle{this, domain, entry};
}

void CacheDiagnostics::unregister(CacheDomain domain, Entry* entry) {
    // P1-10 sealed — single O(1) lookup path. The Handle carries the
    // `domain` field set at registration time, so we never iterate the
    // outer `m_entries` map. Old implementation was O(D) where D =
    // number of distinct domains (~7–10 CacheDomain enum values);
    // new implementation is O(1) average (unordered_map::find +
    // unordered_set::find + unordered_set::erase).
    std::unique_lock lock(m_mutex);
    auto dom_it = m_entries.find(domain);
    if (dom_it != m_entries.end()) {
        if (auto set_it = dom_it->second.find(entry); set_it != dom_it->second.end()) {
            dom_it->second.erase(set_it);
            // Empty per-domain sets accumulate harmlessly (Cat-3
            // minimal-surface; read paths iterate values which are
            // empty sets, no-op). The map-level erase is deferred to
            // future register_cache calls.
        }
    }
    // Handle's RAII contract: destroying the handle MUST free the
    // entry, regardless of whether the entry was found in the
    // registry (it may have been unregistered concurrently, or
    // registered against a stale domain in edge cases).
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
