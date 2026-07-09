// SPDX-License-Identifier: MIT
//
// path_existence_map.cpp — implementation of
// include/chronon3d/render_graph/preflight/path_existence_map.hpp.

#include <chronon3d/render_graph/preflight/path_existence_map.hpp>

#include <mutex>
#include <shared_mutex>
#include <system_error>

namespace chronon3d::preflight {

namespace {

// Internal helper: read one path's existence + mtime without throwing.
// Permission errors are reported as `exists=false` per AGENTS.md
// fail-loud principle (the caller surfaces the missing-asset issue
// rather than entering a hidden retry loop).
PathExistenceMap::Entry stat_path(const std::filesystem::path& path) {
    std::error_code ec_exists;
    const bool exists_now = std::filesystem::exists(path, ec_exists);

    PathExistenceMap::Entry e;
    e.exists = exists_now && !ec_exists;
    e.last_check = std::chrono::steady_clock::now();

    if (e.exists) {
        std::error_code ec_mtime;
        const auto mtime = std::filesystem::last_write_time(path, ec_mtime);
        if (!ec_mtime) {
            e.mtime = mtime;
        }
        // Permission errors on mtime are tolerated: entry stays exists=true
        // and mtime stays default-constructed (caller can still use exists()).
    }
    return e;
}

} // namespace

PathExistenceMap::Entry PathExistenceMap::stat_locked(const std::filesystem::path& path) const {
    return stat_path(path);
}

void PathExistenceMap::populate(const std::vector<std::filesystem::path>& paths) {
    std::unique_lock lock(mu_);
    map_.clear();
    map_.reserve(paths.size());
    for (const auto& p : paths) {
        map_.emplace(p, stat_path(p));
    }
}

void PathExistenceMap::populate(std::initializer_list<std::filesystem::path> paths) {
    std::unique_lock lock(mu_);
    map_.clear();
    map_.reserve(paths.size());
    for (const auto& p : paths) {
        map_.emplace(p, stat_path(p));
    }
}

bool PathExistenceMap::insert(const std::filesystem::path& path) {
    {
        std::shared_lock read_lock(mu_);
        auto it = map_.find(path);
        if (it != map_.end()) {
            // Exists; treat insert as a refresh.
            read_lock.unlock();
            return refresh(path);
        }
    }
    std::unique_lock write_lock(mu_);
    auto [it, inserted] = map_.emplace(path, stat_path(path));
    (void)inserted;
    return it->second.exists;
}

bool PathExistenceMap::exists(const std::filesystem::path& path) {
    auto now = std::chrono::steady_clock::now();
    {
        std::shared_lock read_lock(mu_);
        auto it = map_.find(path);
        if (it != map_.end() && (now - it->second.last_check) < ttl_) {
            return it->second.exists;
        }
    }
    return refresh(path);
}

bool PathExistenceMap::refresh(const std::filesystem::path& path) {
    std::unique_lock write_lock(mu_);
    auto it = map_.find(path);
    if (it == map_.end()) {
        auto [ins, _] = map_.emplace(path, stat_path(path));
        (void)_;
        return ins->second.exists;
    }
    it->second = stat_path(path);
    return it->second.exists;
}

void PathExistenceMap::clear() noexcept {
    std::unique_lock lock(mu_);
    map_.clear();
}

std::size_t PathExistenceMap::size() const noexcept {
    std::shared_lock lock(mu_);
    return map_.size();
}

bool PathExistenceMap::contains(const std::filesystem::path& path) const {
    std::shared_lock lock(mu_);
    return map_.find(path) != map_.end();
}

bool PathExistenceMap::is_fresh(const std::filesystem::path& path) const {
    std::shared_lock lock(mu_);
    auto it = map_.find(path);
    if (it == map_.end()) return false;
    return (std::chrono::steady_clock::now() - it->second.last_check) < ttl_;
}

} // namespace chronon3d::preflight
