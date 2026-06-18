#pragma once

// ---------------------------------------------------------------------------
// frame_conversion_service.hpp — Unified frame conversion service.
//
// Wraps the low-level convert_frame() + ConvertedFrameCache into a single
// call-and-done API.  Handles buffer allocation, cache lookup/insert, and
// aggregated telemetry automatically.
//
// Usage:
//   FrameConversionService svc(8);        // 8-entry LRU cache
//   auto frame = svc.convert(fb, opts);   // ← one call
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
#include <vector>

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

    /// Color matrix: 0 = BT.709, 1 = BT.601, 2 = BT.2020.
    int color_matrix{0};

    /// Enable LRU cache: identical framebuffer digests will reuse
    /// the previously converted bytes without re-running conversion.
    /// Recommended: true for video exports (static/freeze frames).
    bool use_cache{true};
};

/// Result of a single conversion — owned buffer + metadata.
struct ConvertedFrame {
    /// Tightly packed pixel data (no row padding).
    /// For YUV420P: Y (w×h) + U (w/2×h/2) + V (w/2×h/2).
    /// For NV12:    Y (w×h) + interleaved UV (w×h/2).
    /// For RGB24:   packed RGB (w×h×3 bytes).
    std::vector<uint8_t> data;

    /// Number of valid bytes in data (may be < data.capacity()).
    size_t data_size{0};

    /// True if the result came from the cache (no conversion ran).
    bool from_cache{false};

    /// True if the conversion used a SIMD fast path.
    bool used_simd{true};

    /// Wall-clock time spent inside the conversion kernel (nanoseconds).
    uint64_t conversion_ns{0};

    /// Implicitly convertible to bool: true when conversion succeeded.
    explicit operator bool() const noexcept {
        return data_size > 0;
    }
};

/// Centralised frame conversion service.
///
/// Wraps buffer management, cache lookup/insert, and conversion dispatch
/// into a single call.  Keeps reusable buffers alive across calls to
/// reduce heap churn in hot paths.
///
/// NOT thread-safe.  Create one service per encoding thread.
class FrameConversionService {
public:
    /// Construct with an LRU cache of the given capacity.
    /// Capacity 0 disables caching entirely.
    explicit FrameConversionService(size_t cache_capacity = 8);

    // ── Conversion API ────────────────────────────────────────────────

    /// Convert a Framebuffer to the target format.
    /// Allocates an owned buffer and returns it as ConvertedFrame.
    /// On failure, returns a ConvertedFrame with data_size == 0.
    ConvertedFrame convert(const Framebuffer& fb, const ConversionOptions& opts);

    /// Convert into a caller-provided buffer (zero-alloc for output).
    /// `dst` must point to at least `dst_size` bytes.
    /// The required size can be computed with frame_converter::encoded_size().
    /// Returns true on success.
    bool convert_to_buffer(const Framebuffer& fb, const ConversionOptions& opts,
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
    size_t cache_capacity_;
    Stats stats_;

    /// Build a ConvertedFrameCacheKey from a Framebuffer digest and options.
    static ConvertedFrameCacheKey make_cache_key(
        uint64_t digest, const ConversionOptions& opts);

    /// Compute the tight-packing byte count for the given format and dimensions.
    static size_t encoded_size(int width, int height, EncoderPixelFormat fmt);
};

} // namespace chronon3d::video
