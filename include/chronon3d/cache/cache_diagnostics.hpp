#pragma once

// ── cache_diagnostics.hpp — Runtime cache diagnostics service ────────────
//
// cache_diagnostics.hpp — Non-owning runtime registry for cache diagnostics.
// Cache facades register themselves to enable snapshot and clear-by-domain.
// with at construction time.  Provides:
//   - snapshot()          — aggregate stats for all registered caches
//   - snapshot_by_domain()— stats for a single CacheDomain
//   - clear_by_domain()   — clear all caches belonging to one domain
//   - clear_all()         — clear every registered cache
//
// Each cache facade registers a set of callbacks in its constructor and
// holds a Handle (RAII token) that unregisters on destruction.

#include <chronon3d/cache/cache_policy.hpp>
#include <atomic>
#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace chronon3d::cache {

// ── Free function: human-readable cache snapshot dump ──────────────────

/// Query CacheDiagnostics and format a multi-line cache snapshot.
/// Suitable for spdlog::info or CLI output.
[[nodiscard]] std::string format_cache_snapshot();

// ── GenericCacheStats — type-erased LruCache::Stats mirror ────────────────
//
// Because each LruCache<Key,Value,Hash> has its own nested ::Stats type, we
// define a standalone struct here that the registration callbacks return.
// Cache facades convert their LruCache<K,V,H>::Stats → GenericCacheStats
// inside the lambda (trivial field-for-field copy).

struct GenericCacheStats {
    std::size_t hits{0};
    std::size_t misses{0};
    std::size_t evictions{0};
    std::size_t current_size{0};
    std::size_t current_weight{0};
};

// ── CacheSnapshot — aggregate stats for one registered cache instance ─────

struct CacheSnapshot {
    CacheDomain      domain{CacheDomain::Nodes};
    std::size_t      hits{0};
    std::size_t      misses{0};
    std::size_t      evictions{0};
    std::size_t      current_size{0};
    std::size_t      current_weight{0};
    std::size_t      capacity{0};
    CapacityMode     mode{CapacityMode::Weight};
    bool             enabled{true};
};

// ── DomainSnapshot — aggregate across all caches in one domain ────────────

struct DomainSnapshot {
    CacheDomain      domain{CacheDomain::Nodes};
    std::size_t      instance_count{0};
    std::size_t      hits{0};
    std::size_t      misses{0};
    std::size_t      evictions{0};
    std::size_t      current_size{0};
    std::size_t      current_weight{0};
    std::size_t      total_capacity{0};
};

// ── CacheDiagnostics — non-owning registry ────────────────────────────────

class CacheDiagnostics {
public:
    static CacheDiagnostics& instance();

    CacheDiagnostics(const CacheDiagnostics&)            = delete;
    CacheDiagnostics& operator=(const CacheDiagnostics&) = delete;

    // ── Registration ──────────────────────────────────────────────────
    /// Opaque RAII handle.  When destroyed, the cache is unregistered.
    /// Move-only.
    class Handle;

    /// Register a cache instance.  Returns a Handle that MUST be kept
    /// alive for the cache's lifetime; destroying it unregisters.
    ///
    /// @param domain     CacheDomain this cache belongs to.
    /// @param stats_fn   Returns GenericCacheStats for this cache.
    /// @param clear_fn   Clears this cache.
    /// @param mode_fn    Returns the cache's CapacityMode.
    /// @param capacity   Current capacity (bytes or entries).
    [[nodiscard]] Handle register_cache(
        CacheDomain                       domain,
        std::function<GenericCacheStats()> stats_fn,
        std::function<void()>             clear_fn,
        std::function<CapacityMode()>     mode_fn,
        std::size_t                       capacity);

    // ── Snapshot ───────────────────────────────────────────────────────
    [[nodiscard]] std::vector<CacheSnapshot> snapshot() const;
    [[nodiscard]] DomainSnapshot snapshot_by_domain(CacheDomain domain) const;
    [[nodiscard]] std::vector<DomainSnapshot> snapshot_all_domains() const;

    // ── Clear ──────────────────────────────────────────────────────────
    std::size_t clear_by_domain(CacheDomain domain);
    std::size_t clear_all();

    // ── Introspection ──────────────────────────────────────────────────
    [[nodiscard]] std::size_t registered_count() const;
    [[nodiscard]] std::size_t registered_count(CacheDomain domain) const;

    // ── Toggle ─────────────────────────────────────────────────────────
    /// When false, snapshot/clear become no-ops (useful for tests).
    void set_enabled(bool enabled) noexcept;
    [[nodiscard]] bool is_enabled() const noexcept;

private:
    CacheDiagnostics() = default;

    struct Entry {
        std::function<GenericCacheStats()> stats_fn;
        std::function<void()>              clear_fn;
        std::function<CapacityMode()>      mode_fn;
        std::size_t                        capacity{0};
    };

    void unregister(Entry* entry);

    mutable std::mutex                              m_mutex;
    std::unordered_map<CacheDomain, std::vector<Entry*>> m_entries;
    std::atomic<bool>                               m_enabled{true};

    friend class Handle;
};

// ── Handle — RAII registration token ──────────────────────────────────────

class CacheDiagnostics::Handle {
public:
    Handle() = default;

    Handle(CacheDiagnostics* owner, Entry* entry)
        : m_owner(owner), m_entry(entry) {}

    ~Handle() {
        if (m_owner && m_entry) m_owner->unregister(m_entry);
    }

    Handle(Handle&& other) noexcept
        : m_owner(other.m_owner), m_entry(other.m_entry) {
        other.m_owner = nullptr;
        other.m_entry = nullptr;
    }

    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            if (m_owner && m_entry) m_owner->unregister(m_entry);
            m_owner = other.m_owner;
            m_entry = other.m_entry;
            other.m_owner = nullptr;
            other.m_entry = nullptr;
        }
        return *this;
    }

    Handle(const Handle&)            = delete;
    Handle& operator=(const Handle&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept { return m_entry != nullptr; }

private:
    CacheDiagnostics* m_owner{nullptr};
    Entry*            m_entry{nullptr};
};

} // namespace chronon3d::cache

#ifndef CHRONON3D_ENABLE_DIAGNOSTICS

// ── No-op stubs when engine-level diagnostics are disabled at compile time ──

namespace chronon3d::cache {

inline CacheDiagnostics& CacheDiagnostics::instance() {
    static CacheDiagnostics s_instance;
    return s_instance;
}

inline CacheDiagnostics::Handle CacheDiagnostics::register_cache(
    CacheDomain /*domain*/,
    std::function<GenericCacheStats()> /*stats_fn*/,
    std::function<void()> /*clear_fn*/,
    std::function<CapacityMode()> /*mode_fn*/,
    std::size_t /*capacity*/)
{
    return Handle{};
}

inline std::vector<CacheSnapshot> CacheDiagnostics::snapshot() const { return {}; }
inline DomainSnapshot CacheDiagnostics::snapshot_by_domain(CacheDomain domain) const {
    return DomainSnapshot{.domain = domain};
}
inline std::vector<DomainSnapshot> CacheDiagnostics::snapshot_all_domains() const { return {}; }
inline std::size_t CacheDiagnostics::clear_by_domain(CacheDomain) { return 0; }
inline std::size_t CacheDiagnostics::clear_all() { return 0; }
inline std::size_t CacheDiagnostics::registered_count() const { return 0; }
inline std::size_t CacheDiagnostics::registered_count(CacheDomain) const { return 0; }
inline void CacheDiagnostics::set_enabled(bool) noexcept {}
inline bool CacheDiagnostics::is_enabled() const noexcept { return false; }

inline std::string format_cache_snapshot() {
    return "[cache] Diagnostics disabled at compile time (CHRONON3D_ENABLE_DIAGNOSTICS=OFF).\n";
}

} // namespace chronon3d::cache

#endif // CHRONON3D_ENABLE_DIAGNOSTICS
