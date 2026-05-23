#pragma once

#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/core/framebuffer.hpp>
#include <memory>
#include <filesystem>

namespace chronon3d::cache {

class DiskNodeCache {
public:
    static DiskNodeCache& instance();

    void set_cache_dir(const std::filesystem::path& path);
    [[nodiscard]] std::filesystem::path cache_dir() const;

    [[nodiscard]] std::shared_ptr<Framebuffer> get(const NodeCacheKey& key);
    void put(const NodeCacheKey& key, const Framebuffer& fb);

    bool exists(const NodeCacheKey& key) const;
    void clear();

private:
    DiskNodeCache();
    std::filesystem::path m_cache_dir;
};

} // namespace chronon3d::cache
