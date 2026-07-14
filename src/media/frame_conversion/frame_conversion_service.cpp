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
        .matrix             = opts.matrix,
        .range              = opts.range,
        .apply_gamma        = opts.apply_gamma,
    };
}

// ============================================================================
//  Construction
// ============================================================================

FrameConversionService::FrameConversionService(size_t cache_capacity_bytes)
    : cache_(cache_capacity_bytes)
    , cache_capacity_bytes_(cache_capacity_bytes)
{
}

// ============================================================================
//  convert_into()
// ============================================================================

ConvertedFrame FrameConversionService::convert_into(
    const Framebuffer& fb, const ConversionOptions& opts,
    uint8_t* dst, size_t dst_size)
{
    const size_t need = encoded_size(opts.width, opts.height, opts.format);
    if (need == 0 || dst_size < need || dst == nullptr) {
        return ConvertedFrame{};
    }

    const uint64_t digest = fb.key_digest();
    const bool can_cache = opts.use_cache && cache_capacity_bytes_ > 0 && digest != 0;

    // ── Cache lookup ──────────────────────────────────────────────────
    if (can_cache) {
        auto hit = cache_.lookup(make_cache_key(digest, opts));
        if (hit) {
            ++stats_.cache_hits;
            return hit;
        }
        ++stats_.cache_misses;
    }

    // ── Pick output buffer ─────────────────────────────────────────────
    // On the cache-enabled path we allocate cache-owned bytes so the
    // conversion writes directly into the buffer the cache will own:
    //   conversion → cache buffer → move-into entry → encoder reads
    //   the same span → zero extra full-frame copy on miss.
    // On the no-cache path the caller's `dst` remains the destination.
    std::vector<uint8_t> cache_owned;
    uint8_t* out;
    if (can_cache) {
        cache_owned.resize(need);     // exact fit, no realloc churn
        out = cache_owned.data();
    } else {
        out = dst;
    }

    // ── Build plane pointers (cheap, no allocation) ────────────────────
    uint8_t* dst_y  = out;
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
        FramePlanes{
            .y         = dst_y,
            .u         = dst_u,
            .v         = dst_v,
            .uv        = dst_uv,
            .stride_y  = opts.width,
            .stride_u  = opts.width / 2,
            .stride_v  = opts.width / 2,
            .stride_uv = opts.width,
        },
        opts.width, opts.height,
        opts.format,
        opts.matrix, opts.range,
        opts.apply_gamma);

    if (!conv_result.success) {
        return ConvertedFrame{};
    }

    ++stats_.conversions;
    stats_.total_conversion_ns += conv_result.conversion_ns;

    // ── Cache insert (zero-copy on miss) ───────────────────────────────
    if (can_cache) {
        // `cache_owned` is moved into the freshly minted cache entry.
        // The returned ConvertedFrame's `data` span and `cache_entry`
        // shared_ptr reference the SAME bytes the converter just wrote —
        // the encoder reads the exact bytes the cache now owns.
        auto inserted = cache_.insert(
            make_cache_key(digest, opts), std::move(cache_owned));
        ConvertedFrame result;
        result.backend      = conv_result.backend;
        result.conversion_ns = conv_result.conversion_ns;
        if (inserted) {
            result.cache_entry = inserted.cache_entry;
            result.data        = inserted.data;
            result.from_cache  = true;
        }
        // If insert returned empty (defensive — currently does not),
        // fall through with a zero-length span: caller treats it as miss.
        return result;
    }

    // ── No-cache path: span over the caller's `dst` ────────────────────
    ConvertedFrame result;
    result.data          = std::span<const uint8_t>(dst, need);
    result.backend       = conv_result.backend;
    result.conversion_ns = conv_result.conversion_ns;
    return result;
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
