#pragma once

// ---------------------------------------------------------------------------
// frame_conversion_service.hpp — Unified frame conversion service.
//
// Wraps the low-level convert_frame() + ConvertedFrameCache into a single
// call-and-done API.  Handles buffer allocation, cache lookup/insert, and
// aggregated telemetry automatically.
//
// Usage:
//   FrameConversionService svc;                 // byte-capacity LRU cache
//   auto frame = svc.convert_into(fb, opts, dst, dst_size);  // ← one call
//   if (frame) { /* frame.data is ready */ }
//
// Design:
//  - Owns a ConvertedFrameCache internally — callers don't manage it
//  - Reuses a staging buffer across calls (avoids per-frame heap churn)
//  - Tracks aggregate stats (total conversions, wall time, hit rate)
//  - Thread-safe? SAME as the underlying ConvertedFrameCache: YES after
//    Commit 3 (sharded LruCache with per-shard mutex).  Multiple encoder
//    threads can share a single FrameConversionService.  Aggregate
//    counters themselves are NOT atomically incremented across threads
//    (the cache_hits/cache_misses delegates are), so to read consistent
//    stats, callers should still serialise before calling stats().
// ---------------------------------------------------------------------------

#include <chronon3d/media/frame_conversion/frame_converter.hpp>
#include <chronon3d/media/frame_conversion/converted_frame_cache.hpp>
#include <cstddef>
#include <cstdint>

namespace chronon3d::video {

/// What to convert and how.
struct ConversionOptions {
    /// Output width (must match the Framebuffer, included for safety).
    int width{0};

    /// Output height (must match the Framebuffer).
    int height{0};

    /// Target pixel format for the encoded output.
    EncoderPixelFormat format{EncoderPixelFormat::YUV420P};

    /// Apply sRGB gamma curve.  Set false for already-gamma-encoded data.
    bool apply_gamma{true};

    /// Color matrix.  Default BT.709 (modern SDR).  Use BT.601 for SD content
    /// or BT.2020 for HDR / UHDTV.
    YuvMatrix matrix{YuvMatrix::BT709};

    /// Color range of the output samples.
    ColorRange range{ColorRange::Limited};

    /// Enable LRU cache: identical framebuffer digests will reuse
    /// the previously converted bytes without re-running conversion.
    /// Recommended: true for video exports (static/freeze frames).
    bool use_cache{true};
};

/// Result of a single conversion — non-owning view + metadata.
///
/// On a cache hit `cache_entry` holds the cached entry and `data` spans
/// its bytes.  On a cache miss `cache_entry` is null and `data` spans
/// the caller-provided destination buffer that was passed to
/// FrameConversionService::convert_into().

/// Centralised frame conversion service.
///
/// Wraps buffer management, cache lookup/insert, and conversion dispatch
/// into a single call.  Keeps reusable buffers alive across calls to
/// reduce heap churn in hot paths.
///
/// NOT thread-safe.  Create one service per encoding thread.
class FrameConversionService {
public:
    /// Construct with an LRU cache of the given byte capacity.
    /// Capacity 0 disables caching entirely.
    explicit FrameConversionService(size_t cache_capacity_bytes = 0);

    // ── Conversion API ────────────────────────────────────────────────

    /// Convert a Framebuffer into a caller-provided buffer.
    ///
    /// On cache hit the returned ConvertedFrame spans the cached bytes
    /// (no copy into `dst`).  On cache miss the conversion writes directly
    /// into `dst` and the returned view spans `dst`.
    ///
    /// `dst` must point to at least `dst_size` bytes.  The required size
    /// can be computed with FrameConversionService::encoded_size().
    /// Returns an non-empty ConvertedFrame on success.
    ConvertedFrame convert_into(const Framebuffer& fb, const ConversionOptions& opts,
                                uint8_t* dst, size_t dst_size);

    // ── Cache control ─────────────────────────────────────────────────

    /// Clear all cached entries and reset cache counters.
    void clear_cache();

    // ── Aggregated statistics ─────────────────────────────────────────

    struct Stats {
        size_t conversions{0};
        size_t cache_hits{0};
        size_t cache_misses{0};
        uint64_t total_conversion_ns{0};
    };

    [[nodiscard]] Stats stats() const noexcept;

    /// Reset aggregated statistics (does NOT clear the cache).
    void reset_stats() noexcept;

private:
    ConvertedFrameCache cache_;
    size_t cache_capacity_bytes_;
    Stats stats_;

    /// Build a ConvertedFrameCacheKey from a Framebuffer digest and options.
    static ConvertedFrameCacheKey make_cache_key(
        uint64_t digest, const ConversionOptions& opts);

    /// Compute the tight-packing byte count for the given format and dimensions.
    static size_t encoded_size(int width, int height, EncoderPixelFormat fmt);
};

} // namespace chronon3d::video
