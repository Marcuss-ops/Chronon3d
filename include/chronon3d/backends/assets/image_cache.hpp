#pragma once

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/backends/image/image_backend.hpp>
#include <chronon3d/cache/lru_cache.hpp>
#include <string>
#include <memory>
#include <shared_mutex>
#ifdef CHRONON3D_USE_BLEND2D
#include <blend2d.h>
#endif

namespace chronon3d {

struct CachedImage {
    int width{0};
    int height{0};
#ifdef CHRONON3D_USE_BLEND2D
    BLImage bl_img;
#endif
    std::shared_ptr<Framebuffer> fb_img;

    [[nodiscard]] bool valid() const {
#ifdef CHRONON3D_USE_BLEND2D
        return !bl_img.empty() && fb_img && width > 0 && height > 0;
#else
        return fb_img && width > 0 && height > 0;
#endif
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// Fase B B1+B4 — ImageCache is now per-runtime (RenderRuntime-owned).
//
// B1 Removed: instance() singleton, preload_async (std::thread::detach hazard),
//             raw-pointer get_or_load().
// B4 Verified: zero .detach() calls remaining in src/ + include/.
//             Cancellation/join/error-propagation deferred to post-freeze
//             (requires executor-owned job abstraction not safe under freeze).
//
// Owned by RenderRuntime; accessed via runtime.image_cache().
// ═══════════════════════════════════════════════════════════════════════════

class ImageCache {
public:
    explicit ImageCache(size_t capacity_bytes = 512ULL * 1024 * 1024);
    ~ImageCache() = default;
    ImageCache(const ImageCache&) = delete;
    ImageCache& operator=(const ImageCache&) = delete;
    ImageCache(ImageCache&&) noexcept = default;
    ImageCache& operator=(ImageCache&&) noexcept = default;

    // ── Capacity ────────────────────────────────────────────────────
    static void set_global_capacity_bytes(size_t capacity_bytes);

    void set_backend(std::shared_ptr<image::ImageBackend> backend) {
        std::unique_lock<std::shared_mutex> lock(*m_backend_mutex);
        m_backend = std::move(backend);
    }

    [[nodiscard]] std::shared_ptr<image::ImageBackend> get_backend() {
        std::shared_lock<std::shared_mutex> lock(*m_backend_mutex);
        return m_backend;
    }

    /// Load an image from path (synchronous).  Returns shared_ptr to
    /// immutable CachedImage, or nullptr on failure.
    [[nodiscard]] std::shared_ptr<const CachedImage>
    get_or_load(const std::string& path);

    void clear();
    [[nodiscard]] usize size() const {
        return m_cache.stats().current_size;
    }

private:
    // m_backend is set rarely (once at startup) and read on every cache lookup.
    // shared_mutex lets concurrent get_or_load_shared() readers proceed in parallel
    // while set_backend() takes an exclusive lock. The previous std::mutex
    // serialized all backend reads with cache loads.
    //
    // Wrapped in unique_ptr because std::shared_mutex is non-movable;
    // ImageCache must remain movable for RenderRuntime::create() factory.
    std::shared_ptr<image::ImageBackend> m_backend;
    mutable std::unique_ptr<std::shared_mutex> m_backend_mutex{
        std::make_unique<std::shared_mutex>()};

    // The LruCache is already sharded internally (16 shards by default), so
    // concurrent get_or_load_shared() lookups for different paths proceed in
    // parallel. The loader is invoked while holding the per-shard lock, which
    // serializes only concurrent loads of the same path (same shard).
    cache::LruCache<std::string, std::shared_ptr<CachedImage>> m_cache;
};

} // namespace chronon3d
