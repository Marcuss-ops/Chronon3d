// SPDX-License-Identifier: MIT
//
// path_existence_map.hpp — instance-based TTL-aware cache of
// std::filesystem::exists checks. Populated eagerly when the asset set
// is known (composition-finalise time; replaces per-frame sync
// filesystem syscalls in preflight hot paths).
//
// ── AGENTS.md compliance ──
//   * NOT a singleton. Each composition/render-context owns its own
//     instance and threads it (or nullptr → sync fallback) through
//     preflight functions. Per "no singletons/registry/resolver/cache
//     without ADR" rule (this file IS the cache, owner is the caller,
//     no global state).
//   * Fail-loud on filesystem errors: a permission-denied stat during
//     populate is stored as `exists=false` so the caller surfaces the
//     missing-asset issue rather than entering a hidden retry loop.
//
// ── Invalidation strategy ──
//   TTL-based (default 1000 ms). The path is hashed into an
//   unordered_map; once an entry's TTL elapses, the next `exists()`
//   call refreshes it (one syscall). Steady-state hot loops therefore
//   incur ZERO syscalls per lookup but pick up filesystem changes
//   within one TTL. Manual `refresh(path)` and `clear()` APIs are
//   provided for caller-driven invalidation (e.g. file-watcher
//   callbacks on assets/ directory).
//
// ── Why not mtime-compare-on-every-exists() ──
//   `std::filesystem::last_write_time` is itself a stat syscall; the
//   cost is identical to a fresh `exists()` and would defeat the cache
//   purpose. TTL defeats this by amortising the cost to ≥N lookups.

#pragma once

#include <chrono>
#include <filesystem>
#include <initializer_list>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace chronon3d::preflight {

class PathExistenceMap {
public:
    struct Entry {
        bool exists{false};
        std::filesystem::file_time_type mtime{};
        std::chrono::steady_clock::time_point last_check{};
    };

    explicit PathExistenceMap(std::chrono::milliseconds ttl = std::chrono::milliseconds{1000})
        : ttl_(ttl) {}

    PathExistenceMap(const PathExistenceMap&) = delete;
    PathExistenceMap& operator=(const PathExistenceMap&) = delete;
    PathExistenceMap(PathExistenceMap&&) = default;
    PathExistenceMap& operator=(PathExistenceMap&&) = default;

    // Bulk populate: clear existing entries, stat each path, insert.
    // Cheap-ish (one stat per path); called only at scene-build time.
    void populate(const std::vector<std::filesystem::path>& paths);
    void populate(std::initializer_list<std::filesystem::path> paths);

    // Lazy single-path populate (or refresh if already present).
    bool insert(const std::filesystem::path& path);

    // Fast lookup with auto-refresh on TTL expiry.
    //   * entry fresh  → returns cached existence (no syscall).
    //   * entry stale  → refreshes the entry and returns the new value.
    //   * entry absent → inserts and returns the new value.
    // Not marked const because the stale/absent cases mutate the map.
    bool exists(const std::filesystem::path& path);

    // Force refresh of a single entry. Returns the fresh existence value.
    // Inserts if absent (same as `exists()` for the absent case).
    bool refresh(const std::filesystem::path& path);

    // Drop all entries.
    void clear() noexcept;

    // TTL controls (settable post-construction).
    void set_ttl(std::chrono::milliseconds ttl) noexcept { ttl_ = ttl; }
    [[nodiscard]] std::chrono::milliseconds ttl() const noexcept { return ttl_; }

    // Observability / test helpers.
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool contains(const std::filesystem::path& path) const;
    [[nodiscard]] bool is_fresh(const std::filesystem::path& path) const;

private:
    using Map = std::unordered_map<std::filesystem::path, Entry>;
    mutable std::shared_mutex mu_;
    Map map_;
    std::chrono::milliseconds ttl_;

    // Compute a fresh entry by stat. Caller holds the unique lock.
    Entry stat_locked(const std::filesystem::path& path) const;
};

} // namespace chronon3d::preflight
