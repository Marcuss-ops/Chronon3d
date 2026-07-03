#include <chronon3d/backends/assets/image_cache.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/simd/kernels.hpp>
#include <spdlog/spdlog.h>
#include <atomic>
#include <cstdlib>

namespace chronon3d {

namespace {
    size_t            s_image_cache_capacity = 0;
    std::atomic<bool> s_image_cache_capacity_flag{false};
} // namespace

void ImageCache::set_global_capacity_bytes(size_t capacity_bytes) {
    bool expected = false;
    if (s_image_cache_capacity_flag.compare_exchange_strong(
            expected, true,
            std::memory_order_acq_rel, std::memory_order_relaxed)) {
        s_image_cache_capacity = capacity_bytes;
    }
}

static size_t resolve_capacity() {
    constexpr size_t kFallback = 512ULL * 1024ULL * 1024ULL;
    return s_image_cache_capacity > 0 ? s_image_cache_capacity : kFallback;
}

ImageCache::ImageCache(size_t capacity_bytes)
    : m_cache(capacity_bytes > 0 ? capacity_bytes : resolve_capacity()) {}

std::shared_ptr<const CachedImage> ImageCache::get_or_load(const std::string& path) {
    std::shared_ptr<image::ImageBackend> backend;
    {
        std::shared_lock<std::shared_mutex> lock(*m_backend_mutex);
        backend = m_backend;
    }
    if (!backend) {
        spdlog::error("ImageCache: cannot load '{}' - no backend set", path);
        return nullptr;
    }

    auto shared = m_cache.compute_if_absent(path,
        [&]() -> std::pair<std::shared_ptr<CachedImage>, size_t> {
            const auto t0 = profiling::now();
            auto buffer = backend->load_image(path);
            if (!buffer || !buffer->pixels) {
                return {std::make_shared<CachedImage>(), 1};
            }

            auto entry = std::make_shared<CachedImage>();
            entry->width = buffer->width;
            entry->height = buffer->height;

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

            // Fase B1: weight now accounts for both BLImage (PRGB32, 4 bytes/px)
            // and the float Framebuffer (4 floats/px = 16 bytes/px).
            const size_t weight = static_cast<size_t>(entry->width) * entry->height * 4  // BLImage
#ifdef CHRONON3D_USE_BLEND2D
                                + static_cast<size_t>(entry->width) * entry->height * 16 // Framebuffer float
#endif
                                ;
            return {entry, weight};
        });

    if (!shared) {
        return nullptr;
    }
    if (!shared->valid()) {
        spdlog::warn("ImageCache: '{}' is too large ({:.1f} MB) to fit in the cache",
                     path, static_cast<double>(shared->width) * shared->height * 4 / (1024.0 * 1024.0));
        return nullptr;
    }
    return shared;
}

void ImageCache::clear() {
    m_cache.clear();
}

} // namespace chronon3d
