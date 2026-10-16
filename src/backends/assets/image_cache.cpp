#include <chronon3d/backends/assets/image_cache.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/color/output_transform.hpp>
#ifdef CHRONON3D_ENABLE_OCIO
#include <chronon3d/color/ocio_color_transform.hpp>
#endif
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/simd/kernels.hpp>
#include <spdlog/spdlog.h>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>

namespace chronon3d {

namespace {

[[nodiscard]] bool decoded_rgba_is_opaque(const std::vector<float>& rgba) noexcept {
    if (rgba.empty() || rgba.size() % 4 != 0) return false;
    for (std::size_t alpha = 3; alpha < rgba.size(); alpha += 4) {
        if (rgba[alpha] < 0.999f) return false;
    }
    return true;
}

// Overlay images are ultimately composited into a 1920x1080 frame. Keeping a
// very tall source portrait at its original resolution expands the cached
// linear/GPU representations to hundreds of megabytes and defeats the LRU,
// causing the same image to be decoded once per frame. Downscale only the
// runtime copy; the authored/downloaded asset remains unchanged.
constexpr int kMaxImageDimension = 2048;

void downscale_for_runtime(image::ImageBuffer& buffer) {
    if (!buffer.has_pixels() || buffer.width <= 0 || buffer.height <= 0 ||
        std::max(buffer.width, buffer.height) <= kMaxImageDimension) {
        return;
    }

    const double scale = static_cast<double>(kMaxImageDimension) /
                         static_cast<double>(std::max(buffer.width, buffer.height));
    const int target_width = std::max(1, static_cast<int>(std::lround(buffer.width * scale)));
    const int target_height = std::max(1, static_cast<int>(std::lround(buffer.height * scale)));
    const auto target_pixel_count = static_cast<std::size_t>(target_width) * target_height;
    auto resized_bytes = buffer.pixels
        ? std::make_unique<u8[]>(target_pixel_count * 4) : nullptr;
    auto resized_linear = buffer.linear_pixels
        ? std::make_unique<float[]>(target_pixel_count * 4) : nullptr;

    for (int y = 0; y < target_height; ++y) {
        const int source_y = std::min(
            buffer.height - 1,
            static_cast<int>((static_cast<double>(y) / target_height) * buffer.height));
        for (int x = 0; x < target_width; ++x) {
            const int source_x = std::min(
                buffer.width - 1,
                static_cast<int>((static_cast<double>(x) / target_width) * buffer.width));
            const auto source_offset = (static_cast<std::size_t>(source_y) * buffer.width + source_x) * 4;
            const auto target_offset = (static_cast<std::size_t>(y) * target_width + x) * 4;
            if (buffer.pixels)
                std::copy_n(buffer.pixels.get() + source_offset, 4,
                            resized_bytes.get() + target_offset);
            else
                std::copy_n(buffer.linear_pixels.get() + source_offset, 4,
                            resized_linear.get() + target_offset);
        }
    }

    spdlog::info("ImageCache: downscaled runtime image ({}x{} -> {}x{})",
                 buffer.width, buffer.height, target_width, target_height);
    buffer.width = target_width;
    buffer.height = target_height;
    buffer.pixels = std::move(resized_bytes);
    buffer.linear_pixels = std::move(resized_linear);
}

Color decode_image_pixel(const image::ImageBuffer& buffer, std::size_t index,
                         ColorSpace input_color_space
#ifdef CHRONON3D_ENABLE_OCIO
                         , const color::OcioColorTransform* ocio_transform
#endif
                         ) {
    if (buffer.linear_pixels) {
        const float* pixel = buffer.linear_pixels.get() + index * 4;
        // Float-backed decoders (notably EXR) already publish the engine's
        // linear BT.709 working values after honoring source chromaticities.
        return {pixel[0], pixel[1], pixel[2], pixel[3]};
    }
    const u8* pixel = buffer.pixels.get() + index * 4;
    Color decoded{pixel[0] / 255.0f, pixel[1] / 255.0f,
                pixel[2] / 255.0f, pixel[3] / 255.0f};
#ifdef CHRONON3D_ENABLE_OCIO
    if (ocio_transform != nullptr) {
        float rgb[3]{decoded.r, decoded.g, decoded.b};
        if (!ocio_transform->apply_rgb(rgb)) {
            const auto invalid = std::numeric_limits<float>::quiet_NaN();
            return {invalid, invalid, invalid, invalid};
        }
        decoded.r = rgb[0]; decoded.g = rgb[1]; decoded.b = rgb[2];
        return decoded.premultiplied();
    }
#endif
    if (input_color_space == ColorSpace::SRGB ||
        input_color_space == ColorSpace::DisplayP3)
        decoded = decoded.to_linear();
    if (input_color_space == ColorSpace::Rec709) {
        decoded.r = color::rec709_to_linear_float(decoded.r);
        decoded.g = color::rec709_to_linear_float(decoded.g);
        decoded.b = color::rec709_to_linear_float(decoded.b);
    }
    if (input_color_space == ColorSpace::DisplayP3) {
        const auto converted = color::display_p3_to_bt709_linear(
            decoded.r, decoded.g, decoded.b);
        decoded.r = converted[0];
        decoded.g = converted[1];
        decoded.b = converted[2];
    }
    return decoded.premultiplied();
}

} // namespace

ImageCache::ImageCache(size_t capacity_bytes)
    : m_cache(capacity_bytes > 0 ? capacity_bytes : 512ULL * 1024ULL * 1024ULL) {}

std::optional<ImageAssetKey> ImageCache::canonical_key(
    const std::string& path, ImageDecodeOptions options) const {
    ImageAssetKey key{.options = options};
    if (!m_asset_resolver) {
        key.canonical_path = std::filesystem::path(path).lexically_normal();
        return key;
    }
    const auto resolved = m_asset_resolver->resolve_lexical(path);
    if (!resolved) {
        return std::nullopt;
    }
    key.canonical_path = resolved->lexically_normal();
    return key;
}

std::optional<ImageAssetKey> ImageCache::canonical_key(
    const std::string& path, ImageDecodeOptions options,
    const assets::AssetResolver& resolver) const {
    ImageAssetKey key{.options = options};
    const auto resolved = resolver.resolve_lexical(path);
    if (!resolved) return std::nullopt;
    key.canonical_path = resolved->lexically_normal();
    return key;
}

std::optional<ImageAssetKey> ImageCache::canonical_key_for(
    const std::string& path, ImageDecodeOptions options) const {
    return canonical_key(path, options);
}

std::optional<ImageAssetKey> ImageCache::canonical_key_for(
    const std::string& path, ImageDecodeOptions options,
    const assets::AssetResolver& resolver) const {
    return canonical_key(path, options, resolver);
}

std::shared_ptr<const CachedImage> ImageCache::get_or_load(
    const std::string& path, ImageDecodeOptions options) {
    // Asset resolve: canonical path + decode-options key resolution.
    const auto resolve_t0 = profiling::now();
    const auto key = canonical_key(path, options);
    const double resolve_us = profiling::duration_us(resolve_t0, profiling::now());
    if (profiling::g_current_counters) {
        profiling::g_current_counters->image_resolve_wall_us.fetch_add(
            static_cast<uint64_t>(std::llround(resolve_us)), std::memory_order_relaxed);
    }
    if (!key) {
        spdlog::warn("ImageCache: asset '{}' is outside the engine asset root", path);
        return nullptr;
    }
    const std::string resolved_path = key->canonical_path.string();
    std::shared_ptr<image::ImageBackend> backend;
    {
        std::shared_lock<std::shared_mutex> lock(*m_backend_mutex);
        backend = m_backend;
    }
    if (!backend) {
        spdlog::error("ImageCache: cannot load '{}' - no backend set", path);
        return nullptr;
    }

    // TICKET-CACHE-COUNTERS-V1 — count image decode-cache hit/miss so
    // decode-once (miss ≈ 1) vs decode-per-frame (miss ≈ frames) is visible
    // in telemetry for the static-asset benchmark.
    if (profiling::g_current_counters) {
        if (m_cache.contains(*key)) {
            profiling::g_current_counters->image_cache_hits.fetch_add(1, std::memory_order_relaxed);
        } else {
            profiling::g_current_counters->image_cache_misses.fetch_add(1, std::memory_order_relaxed);
        }
    }

    auto shared = m_cache.compute_if_absent(*key,
        [&]() -> std::pair<std::shared_ptr<CachedImage>, size_t> {
            const auto t0 = profiling::now();
            // Decode: backend loads + decodes the image bytes into raw RGBA.
            const auto decode_t0 = profiling::now();
            auto buffer = backend->load_image(resolved_path);
            const double decode_us = profiling::duration_us(decode_t0, profiling::now());
            if (profiling::g_current_counters) {
                profiling::g_current_counters->image_decode_wall_us.fetch_add(
                    static_cast<uint64_t>(std::llround(decode_us)), std::memory_order_relaxed);
                profiling::g_current_counters->image_decode_count.fetch_add(1, std::memory_order_relaxed);
            }
            if (!buffer || !buffer->has_pixels()) {
                return {std::make_shared<CachedImage>(), 1};
            }

#ifdef CHRONON3D_ENABLE_OCIO
            std::optional<color::OcioColorTransform> ocio_transform;
            if (!options.ocio_input_space.empty()) {
                if (buffer->linear_pixels) {
                    spdlog::error("ImageCache: OCIO input space on float image '{}' is unsupported; EXR is already decoded to linear BT.709", path);
                    return {std::make_shared<CachedImage>(), 1};
                }
                std::string error;
                ocio_transform = color::OcioColorTransform::from_config(
                    options.ocio_config_path, options.ocio_input_space,
                    options.ocio_working_space, &error);
                if (!ocio_transform) {
                    spdlog::error("ImageCache: OCIO input transform for '{}' failed: {}", path, error);
                    return {std::make_shared<CachedImage>(), 1};
                }
            }
#else
            if (!options.ocio_input_space.empty()) {
                spdlog::error("ImageCache: '{}' declares ocio_input_space, but Chronon was built without OpenColorIO", path);
                return {std::make_shared<CachedImage>(), 1};
            }
#endif

            downscale_for_runtime(*buffer);

            auto entry = std::make_shared<CachedImage>();
            entry->width = buffer->width;
            entry->height = buffer->height;

            // Convert: premultiply + sRGB→linear color conversion into the
            // Blend2D BLImage and the float Framebuffer.
            const auto convert_t0 = profiling::now();

#ifdef CHRONON3D_USE_BLEND2D
            if (buffer->pixels && options.color_space == ColorSpace::SRGB &&
                options.ocio_input_space.empty()) {
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
            }
#endif

            entry->fb_img = std::make_shared<Framebuffer>(entry->width, entry->height);
            if (entry->fb_img) {
                entry->gpu_rgba.resize(static_cast<std::size_t>(entry->width) * entry->height * 4);
                // Each row is independent. Parallelizing this conversion is
                // safe because both destinations are pre-sized and indexed
                // by row; it removes the serial sRGB->linear bottleneck while
                // preserving the exact premultiplied-color operation.
                tbb::parallel_for(tbb::blocked_range<int>(0, entry->height),
                    [&](const tbb::blocked_range<int>& rows) {
                        for (int y = rows.begin(); y < rows.end(); ++y) {
                            Color* dst_row = entry->fb_img->pixels_row(y);
                            float* gpu_row = entry->gpu_rgba.data() +
                                static_cast<std::size_t>(y) * entry->width * 4;
                            for (int x = 0; x < entry->width; ++x) {
                                const auto index = static_cast<std::size_t>(y) * entry->width + x;
                                const auto color = decode_image_pixel(
                                    *buffer, index, options.color_space
#ifdef CHRONON3D_ENABLE_OCIO
                                    , ocio_transform ? &*ocio_transform : nullptr
#endif
                                    );
                                dst_row[x] = color;
                                gpu_row[x * 4 + 0] = color.r;
                                gpu_row[x * 4 + 1] = color.g;
                                gpu_row[x * 4 + 2] = color.b;
                                gpu_row[x * 4 + 3] = color.a;
                            }
                        }
                    });
                entry->fb_img->set_opaque(decoded_rgba_is_opaque(entry->gpu_rgba));
#ifdef CHRONON3D_ENABLE_OCIO
                if (ocio_transform && std::any_of(entry->gpu_rgba.begin(),
                    entry->gpu_rgba.end(), [](float value) { return !std::isfinite(value); })) {
                    spdlog::error("ImageCache: OCIO input transform produced invalid pixels for '{}'", path);
                    return {std::make_shared<CachedImage>(), 1};
                }
#endif
                const std::string_view bytes(
                    reinterpret_cast<const char*>(entry->gpu_rgba.data()),
                    entry->gpu_rgba.size() * sizeof(float));
                entry->gpu_key = runtime::GpuAssetKey{
                    assets::sha256_string(bytes), runtime::PixelFormat::Rgba32Float,
                    static_cast<std::uint32_t>(entry->width),
                    static_cast<std::uint32_t>(entry->height)};
            }

            const double convert_us = profiling::duration_us(convert_t0, profiling::now());
            if (profiling::g_current_counters) {
                profiling::g_current_counters->image_convert_wall_us.fetch_add(
                    static_cast<uint64_t>(std::llround(convert_us)), std::memory_order_relaxed);
            }

            const double load_dur_ms = profiling::elapsed_ms(t0);
            spdlog::info("ImageCache: loaded '{}' ({}x{}) in {:.3f}ms", path, entry->width, entry->height, load_dur_ms);

            // Fase B1: weight now accounts for both BLImage (PRGB32, 4 bytes/px)
            // and the float Framebuffer (4 floats/px = 16 bytes/px).
            const size_t weight = static_cast<size_t>(entry->width) * entry->height * 4  // BLImage
#ifdef CHRONON3D_USE_BLEND2D
                                + static_cast<size_t>(entry->width) * entry->height * 16 // Framebuffer float
#endif
                                + entry->gpu_rgba.size() * sizeof(float);
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

std::shared_ptr<const CachedImage> ImageCache::find(
    const std::string& path, ImageDecodeOptions options) {
    const auto key = canonical_key(path, options);
    if (!key) {
        return nullptr;
    }
    const auto cached = m_cache.get(*key);
    return cached ? *cached : nullptr;
}

std::shared_ptr<const CachedImage> ImageCache::find(
    const std::string& path, ImageDecodeOptions options,
    const assets::AssetResolver& resolver) {
    const auto key = canonical_key(path, options, resolver);
    if (!key) return nullptr;
    const auto cached = m_cache.get(*key);
    return cached ? *cached : nullptr;
}

// Resolver-explicit load: never touches m_asset_resolver, so a shared
// device cache cannot be polluted by a job-scoped pointer.
std::shared_ptr<const CachedImage> ImageCache::get_or_load(
    const std::string& path, ImageDecodeOptions options,
    const assets::AssetResolver& resolver) {
    const auto resolve_t0 = profiling::now();
    const auto key = canonical_key(path, options, resolver);
    const double resolve_us = profiling::duration_us(resolve_t0, profiling::now());
    if (profiling::g_current_counters) {
        profiling::g_current_counters->image_resolve_wall_us.fetch_add(
            static_cast<uint64_t>(std::llround(resolve_us)), std::memory_order_relaxed);
    }
    if (!key) {
        spdlog::warn("ImageCache: asset '{}' is outside the engine asset root", path);
        return nullptr;
    }
    const std::string resolved_path = key->canonical_path.string();
    std::shared_ptr<image::ImageBackend> backend;
    {
        std::shared_lock<std::shared_mutex> lock(*m_backend_mutex);
        backend = m_backend;
    }
    if (!backend) {
        spdlog::error("ImageCache: cannot load '{}' - no backend set", path);
        return nullptr;
    }
    if (profiling::g_current_counters) {
        if (m_cache.contains(*key)) {
            profiling::g_current_counters->image_cache_hits.fetch_add(1, std::memory_order_relaxed);
        } else {
            profiling::g_current_counters->image_cache_misses.fetch_add(1, std::memory_order_relaxed);
        }
    }
    auto shared = m_cache.compute_if_absent(*key,
        [&]() -> std::pair<std::shared_ptr<CachedImage>, size_t> {
            const auto t0 = profiling::now();
            const auto decode_t0 = profiling::now();
            auto buffer = backend->load_image(resolved_path);
            const double decode_us = profiling::duration_us(decode_t0, profiling::now());
            if (profiling::g_current_counters) {
                profiling::g_current_counters->image_decode_wall_us.fetch_add(
                    static_cast<uint64_t>(std::llround(decode_us)), std::memory_order_relaxed);
                profiling::g_current_counters->image_decode_count.fetch_add(1, std::memory_order_relaxed);
            }
            if (!buffer || !buffer->has_pixels()) {
                return {std::make_shared<CachedImage>(), 1};
            }
#ifdef CHRONON3D_ENABLE_OCIO
            std::optional<color::OcioColorTransform> ocio_transform;
            if (!options.ocio_input_space.empty()) {
                if (buffer->linear_pixels) {
                    spdlog::error("ImageCache: OCIO input space on float image '{}' is unsupported; EXR is already decoded to linear BT.709", path);
                    return {std::make_shared<CachedImage>(), 1};
                }
                std::string error;
                ocio_transform = color::OcioColorTransform::from_config(
                    options.ocio_config_path, options.ocio_input_space,
                    options.ocio_working_space, &error);
                if (!ocio_transform) {
                    spdlog::error("ImageCache: OCIO input transform for '{}' failed: {}", path, error);
                    return {std::make_shared<CachedImage>(), 1};
                }
            }
#else
            if (!options.ocio_input_space.empty()) {
                spdlog::error("ImageCache: '{}' declares ocio_input_space, but Chronon was built without OpenColorIO", path);
                return {std::make_shared<CachedImage>(), 1};
            }
#endif
            downscale_for_runtime(*buffer);
            auto entry = std::make_shared<CachedImage>();
            entry->width = buffer->width;
            entry->height = buffer->height;
#ifdef CHRONON3D_USE_BLEND2D
            if (buffer->pixels && options.color_space == ColorSpace::SRGB &&
                options.ocio_input_space.empty()) {
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
            }
#endif
            entry->fb_img = std::make_shared<Framebuffer>(entry->width, entry->height);
            if (entry->fb_img) {
                entry->gpu_rgba.resize(static_cast<std::size_t>(entry->width) * entry->height * 4);
                tbb::parallel_for(tbb::blocked_range<int>(0, entry->height),
                    [&](const tbb::blocked_range<int>& rows) {
                        for (int y = rows.begin(); y < rows.end(); ++y) {
                            Color* dst_row = entry->fb_img->pixels_row(y);
                            float* gpu_row = entry->gpu_rgba.data() +
                                static_cast<std::size_t>(y) * entry->width * 4;
                            for (int x = 0; x < entry->width; ++x) {
                                const auto index = static_cast<std::size_t>(y) * entry->width + x;
                                const auto color = decode_image_pixel(
                                    *buffer, index, options.color_space
#ifdef CHRONON3D_ENABLE_OCIO
                                    , ocio_transform ? &*ocio_transform : nullptr
#endif
                                    );
                                dst_row[x] = color;
                                gpu_row[x * 4 + 0] = color.r;
                                gpu_row[x * 4 + 1] = color.g;
                                gpu_row[x * 4 + 2] = color.b;
                                gpu_row[x * 4 + 3] = color.a;
                            }
                        }
                    });
                entry->fb_img->set_opaque(decoded_rgba_is_opaque(entry->gpu_rgba));
#ifdef CHRONON3D_ENABLE_OCIO
                if (ocio_transform && std::any_of(entry->gpu_rgba.begin(),
                    entry->gpu_rgba.end(), [](float value) { return !std::isfinite(value); })) {
                    spdlog::error("ImageCache: OCIO input transform produced invalid pixels for '{}'", path);
                    return {std::make_shared<CachedImage>(), 1};
                }
#endif
                const std::string_view bytes(
                    reinterpret_cast<const char*>(entry->gpu_rgba.data()),
                    entry->gpu_rgba.size() * sizeof(float));
                entry->gpu_key = runtime::GpuAssetKey{
                    assets::sha256_string(bytes), runtime::PixelFormat::Rgba32Float,
                    static_cast<std::uint32_t>(entry->width),
                    static_cast<std::uint32_t>(entry->height)};
            }
            const double convert_us = profiling::duration_us(profiling::now(), profiling::now());
            (void)convert_us;
            const double load_dur_ms = profiling::elapsed_ms(t0);
            spdlog::info("ImageCache: loaded '{}' ({}x{}) in {:.3f}ms", path, entry->width, entry->height, load_dur_ms);
            const size_t weight = static_cast<size_t>(entry->width) * entry->height * 4
#ifdef CHRONON3D_USE_BLEND2D
                                 + static_cast<size_t>(entry->width) * entry->height * 16
#endif
                                 + entry->gpu_rgba.size() * sizeof(float);
            return std::pair<std::shared_ptr<CachedImage>, size_t>{entry, weight};
        });
    if (!shared) return nullptr;
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
