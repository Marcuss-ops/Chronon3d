#include <chronon3d/backends/assets/image_cache.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/simd/kernels.hpp>
#include <spdlog/spdlog.h>
#include <atomic>
#include <cstdlib>
#include <mutex>
#include <thread>

namespace chronon3d {

// FASE 3 (TICKET-079) — Injected capacity, set once at startup by SoftwareRenderer.
// First-call-wins via atomic CAS; eliminates std::once_flag + std::call_once (per
// AGENTS.md pattern `is serialised + idempotent without an external std::once_flag`).
namespace {

    size_t            s_image_cache_capacity = 0;
    std::atomic<bool> s_image_cache_capacity_flag{false};

} // namespace

void ImageCache::set_capacity_bytes(size_t capacity_bytes) {
    bool expected = false;
    if (s_image_cache_capacity_flag.compare_exchange_strong(
            expected, true,
            std::memory_order_acq_rel, std::memory_order_relaxed)) {
        s_image_cache_capacity = capacity_bytes;
    }
}

static size_t resolve_injected_capacity() {
    constexpr size_t kFallback = 512ULL * 1024ULL * 1024ULL;
    return s_image_cache_capacity > 0 ? s_image_cache_capacity : kFallback;
}

// Fase B (B4) — std::thread::detach() is a known hazard:
//   - no cancellation mechanism
//   - no join / ownership of the job
//   - no error propagation
//   - no guarantee backend + logging survive through shutdown
//
// Migrate to executor-owned preload jobs (Phase C).
void ImageCache::preload_async(const std::string& path) {
    std::thread([path]() {
        instance().get_or_load(path);
    }).detach();
}

ImageCache::ImageCache()
    : m_cache(resolve_injected_capacity()) {}

const CachedImage* ImageCache::get_or_load(const std::string& path) {
    auto shared = get_or_load_shared(path);
    return shared.get();
}

std::shared_ptr<const CachedImage> ImageCache::get_or_load_shared(const std::string& path) {
    // Snapshot the backend pointer under a shared lock so concurrent readers
    // don't block each other.  The refcount increment on the shared_ptr is
    // cheap and the lock is released before any I/O happens.
    std::shared_ptr<image::ImageBackend> backend;
    {
        std::shared_lock<std::shared_mutex> lock(m_backend_mutex);
        backend = m_backend;
    }
    if (!backend) {
        spdlog::error("ImageCache: cannot load '{}' - no backend set", path);
        return nullptr;
    }

    // compute_if_absent() does the double-checked-locking internally on the
    // per-shard mutex, so we no longer need a process-wide mutex here.  The
    // loader is invoked while holding only the shard's lock, so concurrent
    // loads of images that hash to different shards run in parallel.
    auto shared = m_cache.compute_if_absent(path,
        [&]() -> std::pair<std::shared_ptr<CachedImage>, size_t> {
            const auto t0 = profiling::now();
            auto buffer = backend->load_image(path);
            if (!buffer || !buffer->pixels) {
                // Cache a null entry (weight 1) so repeated lookups for a
                // missing file don't re-hit the disk on every frame.
                return {std::make_shared<CachedImage>(), 1};
            }

            auto entry = std::make_shared<CachedImage>();
            entry->width = buffer->width;
            entry->height = buffer->height;

            // Create BLImage and premultiply alpha for Blend2D (PRGB32)
#ifdef CHRONON3D_USE_BLEND2D
            entry->bl_img.create(entry->width, entry->height, BL_FORMAT_PRGB32);
            BLImageData bl_data;
            if (entry->bl_img.getData(&bl_data) == BL_SUCCESS) {
                uint32_t* dst = static_cast<uint32_t*>(bl_data.pixelData);
                const uint8_t* src = buffer->pixels.get();
                const int stride = static_cast<int>(bl_data.stride / sizeof(uint32_t));

                if (stride == entry->width) {
                    simd::premultiply_alpha_rgba8(dst, src, entry->width * entry->height);
                } else {
                    for (int y = 0; y < entry->height; ++y) {
                        simd::premultiply_alpha_rgba8(dst + y * stride, src + y * entry->width * 4, entry->width);
                    }
                }
            }
#endif

            // Keep a float framebuffer copy for the software raster path. This avoids
            // re-sampling the BLImage path for scaled images, which is where we observed
            // striping artifacts.
            entry->fb_img = std::make_shared<Framebuffer>(entry->width, entry->height);
            if (entry->fb_img) {
                const uint8_t* src = buffer->pixels.get();
                for (int y = 0; y < entry->height; ++y) {
                    Color* dst_row = entry->fb_img->pixels_row(y);
                    for (int x = 0; x < entry->width; ++x) {
                        const int idx = (y * entry->width + x) * 4;
                        const float r = src[idx + 0] / 255.0f;
                        const float g = src[idx + 1] / 255.0f;
                        const float b = src[idx + 2] / 255.0f;
                        const float a = src[idx + 3] / 255.0f;
                        dst_row[x] = Color{r * a, g * a, b * a, a};
                    }
                }
            }

            const double load_dur_ms = profiling::elapsed_ms(t0);
            spdlog::info("ImageCache: loaded '{}' ({}x{}) in {:.3f}ms", path, entry->width, entry->height, load_dur_ms);

            const size_t weight = static_cast<size_t>(entry->width) * entry->height * 4;
            return {entry, weight};
        });

    if (!shared) {
        return nullptr;
    }
    if (!shared->valid()) {
        spdlog::warn("ImageCache: '{}' is too large ({:.1f} MB) to fit in the cache — decoding on every frame",
                     path, static_cast<double>(shared->width) * shared->height * 4 / (1024.0 * 1024.0));
        return nullptr;
    }
    return shared;
}

void ImageCache::clear() {
    m_cache.clear();
}

} // namespace chronon3d
