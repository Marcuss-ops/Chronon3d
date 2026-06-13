#include <chronon3d/media/frame_conversion/frame_conversion_service.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <algorithm>
#include <cstring>

namespace chronon3d::video {

// ============================================================================
//  Helpers
// ============================================================================

size_t FrameConversionService::encoded_size(int width, int height, EncoderPixelFormat fmt) {
    const uint64_t w = static_cast<uint64_t>(width);
    const uint64_t h = static_cast<uint64_t>(height);

    switch (fmt) {
        case EncoderPixelFormat::YUV420P:
            return w * h + (w / 2) * (h / 2) * 2;
        case EncoderPixelFormat::NV12:
            return w * h + (w / 2) * (h / 2) * 2;
        case EncoderPixelFormat::RGB24:
            return w * h * 3;
        case EncoderPixelFormat::RGBA8:
            return w * h * 4;
    }
    return 0;
}

ConvertedFrameCacheKey FrameConversionService::make_cache_key(
    uint64_t digest, const ConversionOptions& opts)
{
    return ConvertedFrameCacheKey{
        .framebuffer_digest = digest,
        .width              = opts.width,
        .height             = opts.height,
        .format             = opts.format,
        .color_matrix       = opts.color_matrix,
        .apply_gamma        = opts.apply_gamma,
    };
}

// ============================================================================
//  Construction
// ============================================================================

FrameConversionService::FrameConversionService(size_t cache_capacity)
    : cache_(cache_capacity)
    , cache_capacity_(cache_capacity)
{
}

// ============================================================================
//  convert()
// ============================================================================

ConvertedFrame FrameConversionService::convert(
    const Framebuffer& fb, const ConversionOptions& opts)
{
    const uint64_t digest = fb.key_digest();
    const bool can_cache = opts.use_cache && cache_capacity_ > 0 && digest != 0;

    // ── Cache lookup ──────────────────────────────────────────────────
    if (can_cache) {
        const auto* hit = cache_.lookup(make_cache_key(digest, opts));
        if (hit) {
            ++stats_.cache_hits;
            ConvertedFrame result;
            result.data.reserve(hit->data_size);
            result.data.assign(hit->data.data(), hit->data.data() + hit->data_size);
            result.data_size = hit->data_size;
            result.from_cache = true;
            result.used_simd = true;
            result.conversion_ns = 0;
            return result;
        }
        ++stats_.cache_misses;
    }

    // ── Allocate output buffer ────────────────────────────────────────
    const size_t need = encoded_size(opts.width, opts.height, opts.format);
    if (need == 0) {
        return ConvertedFrame{};  // invalid dimensions or format
    }

    ConvertedFrame result;
    result.data.resize(need);
    result.data_size = need;

    // ── Build the conversion request ──────────────────────────────────
    uint8_t* dst_y  = result.data.data();
    uint8_t* dst_u  = nullptr;
    uint8_t* dst_v  = nullptr;
    uint8_t* dst_uv = nullptr;

    switch (opts.format) {
        case EncoderPixelFormat::YUV420P: {
            const size_t y_size = static_cast<size_t>(opts.width) * opts.height;
            const size_t uv_size = y_size / 4;
            dst_u = dst_y + y_size;
            dst_v = dst_u + uv_size;
            break;
        }
        case EncoderPixelFormat::NV12: {
            const size_t y_size = static_cast<size_t>(opts.width) * opts.height;
            dst_uv = dst_y + y_size;
            break;
        }
        case EncoderPixelFormat::RGB24:
        case EncoderPixelFormat::RGBA8:
            // dst_y points to the packed output (RGB: 3B/px, RGBA: 4B/px)
            break;
    }

    // Use convert_frame_tight for simplicity (tight strides = no padding).
    const auto conv_result = convert_frame_tight(
        fb,
        dst_y, dst_u, dst_v, dst_uv,
        opts.width, opts.height,
        opts.format,
        opts.apply_gamma);

    if (!conv_result.success) {
        result.data.clear();
        result.data_size = 0;
        return result;
    }

    result.used_simd = conv_result.used_simd;
    result.conversion_ns = conv_result.conversion_ns;
    ++stats_.conversions;
    stats_.total_conversion_ns += conv_result.conversion_ns;

    // ── Cache insert ─────────────────────────────────────────────────
    if (can_cache) {
        cache_.insert(make_cache_key(digest, opts), result.data.data(), result.data_size);
    }

    return result;
}

// ============================================================================
//  convert_to_buffer()
// ============================================================================

bool FrameConversionService::convert_to_buffer(
    const Framebuffer& fb, const ConversionOptions& opts,
    uint8_t* dst, size_t dst_size)
{
    const size_t need = encoded_size(opts.width, opts.height, opts.format);
    if (need == 0 || dst_size < need || dst == nullptr) {
        return false;
    }

    const uint64_t digest = fb.key_digest();
    const bool can_cache = opts.use_cache && cache_capacity_ > 0 && digest != 0;

    // ── Cache lookup ──────────────────────────────────────────────────
    if (can_cache) {
        const auto* hit = cache_.lookup(make_cache_key(digest, opts));
        if (hit) {
            std::memcpy(dst, hit->data.data(), std::min(hit->data_size, dst_size));
            ++stats_.cache_hits;
            return true;
        }
        ++stats_.cache_misses;
    }

    // ── Build plane pointers ──────────────────────────────────────────
    uint8_t* dst_y  = dst;
    uint8_t* dst_u  = nullptr;
    uint8_t* dst_v  = nullptr;
    uint8_t* dst_uv = nullptr;

    switch (opts.format) {
        case EncoderPixelFormat::YUV420P: {
            const size_t y_size = static_cast<size_t>(opts.width) * opts.height;
            const size_t uv_size = y_size / 4;
            dst_u = dst_y + y_size;
            dst_v = dst_u + uv_size;
            break;
        }
        case EncoderPixelFormat::NV12: {
            const size_t y_size = static_cast<size_t>(opts.width) * opts.height;
            dst_uv = dst_y + y_size;
            break;
        }
        case EncoderPixelFormat::RGB24:
            break;
    }

    const auto conv_result = convert_frame_tight(
        fb,
        dst_y, dst_u, dst_v, dst_uv,
        opts.width, opts.height,
        opts.format,
        opts.apply_gamma);

    if (!conv_result.success) {
        return false;
    }

    ++stats_.conversions;
    stats_.total_conversion_ns += conv_result.conversion_ns;

    // ── Cache insert ─────────────────────────────────────────────────
    if (can_cache) {
        cache_.insert(make_cache_key(digest, opts), dst, need);
    }

    return true;
}

// ============================================================================
//  Cache & stats
// ============================================================================

void FrameConversionService::clear_cache() {
    cache_.clear();
}

FrameConversionService::Stats FrameConversionService::stats() const noexcept {
    Stats s = stats_;
    s.cache_hits   = cache_.hits();
    s.cache_misses = cache_.misses();
    return s;
}

void FrameConversionService::reset_stats() noexcept {
    stats_ = Stats{};
}

} // namespace chronon3d::video
