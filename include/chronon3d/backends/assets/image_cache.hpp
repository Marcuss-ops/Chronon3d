#pragma once

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/backends/image/image_backend.hpp>
#include <chronon3d/cache/lru_cache.hpp>
#include <string>
#include <memory>
#include <shared_mutex>

#include <blend2d.h>

namespace chronon3d {

struct CachedImage {
    int width{0};
    int height{0};
    BLImage bl_img;
    std::shared_ptr<Framebuffer> fb_img;

    [[nodiscard]] bool valid() const { return !bl_img.empty() && fb_img && width > 0 && height > 0; }
};

class ImageCache {
public:
    static ImageCache& instance() {
        static ImageCache inst;
        return inst;
    }

    static void preload(const std::string& path) {
        instance().get_or_load(path);
    }

    static void preload_async(const std::string& path);

    static std::shared_ptr<const CachedImage> get(const std::string& path) {
        return instance().get_or_load_shared(path);
    }

    static void set_capacity_bytes(size_t capacity_bytes);

    void set_backend(std::shared_ptr<image::ImageBackend> backend) {
        // unique_lock because we mutate the backend pointer
        std::unique_lock<std::shared_mutex> lock(m_backend_mutex);
        m_backend = std::move(backend);
    }

    [[nodiscard]] std::shared_ptr<image::ImageBackend> get_backend() {
        // shared_lock allows concurrent readers (the hot path)
        std::shared_lock<std::shared_mutex> lock(m_backend_mutex);
        return m_backend;
    }

    const CachedImage* get_or_load(const std::string& path);
    std::shared_ptr<const CachedImage> get_or_load_shared(const std::string& path);
    void clear();
    [[nodiscard]] usize size() {
        return m_cache.stats().current_size;
    }

private:
    ImageCache();
    ~ImageCache() = default;
    ImageCache(const ImageCache&) = delete;
    ImageCache& operator=(const ImageCache&) = delete;

    // m_backend is set rarely (once at startup) and read on every cache lookup.
    // shared_mutex lets concurrent get_or_load_shared() readers proceed in parallel
    // while set_backend() takes an exclusive lock. The previous std::mutex
    // serialized all backend reads with cache loads.
    std::shared_ptr<image::ImageBackend> m_backend;
    mutable std::shared_mutex m_backend_mutex;

    // The LruCache is already sharded internally (16 shards by default), so
    // concurrent get_or_load_shared() lookups for different paths proceed in
    // parallel. The loader is invoked while holding the per-shard lock, which
    // serializes only concurrent loads of the same path (same shard).
    cache::LruCache<std::string, std::shared_ptr<CachedImage>> m_cache;
};

} // namespace chronon3d
