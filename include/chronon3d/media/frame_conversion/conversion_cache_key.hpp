#pragma once

// ---------------------------------------------------------------------------
// conversion_cache_key.hpp — THE authority for "may this already-converted
// frame be reused instead of converting again?".
//
// Both conversion-cache consumers (the shared `FrameConversionService` and the
// native encoder's single-entry cache) build their key here, so eligibility can
// never diverge between them.
//
// Identity source: `Framebuffer::content_digest()` — the producer's assertion
// that the PIXELS are identical. NEVER `Framebuffer::key_digest()`: that is the
// node's cache-KEY digest, shared by every frame the same node renders, so
// treating it as a content identity made the encoder skip the RGBA→YUV
// conversion on every frame after the first and froze rendered video on frame 0.
//
// `content_digest == 0` means "unknown identity" and is the default on every
// framebuffer: the key is then not `usable()` and the caller MUST convert.
// There is deliberately no fallback that hashes the pixels here — hashing
// 1080p RGBA costs about as much as the conversion it would skip, so an
// unknown identity is a miss by construction rather than a hidden per-frame
// cost. A producer that can prove pixel identity stamps the digest once.
// ---------------------------------------------------------------------------

#include <chronon3d/core/memory/framebuffer.hpp>

#include <cstdint>

namespace chronon3d::video {

/// Identity of a converted frame: content bytes + the parameters that shape
/// them. Equal keys ⇒ bit-identical output ⇒ the converted bytes may be reused.
struct ConversionCacheKey {
    uint64_t content_digest{0};   ///< 0 = unknown identity (never reusable).
    int      width{0};
    int      height{0};
    int      pix_fmt{-1};         ///< encoder pixel format (AVPixelFormat / EncoderPixelFormat).
    bool     apply_gamma{false};
    int      color_matrix{-1};    ///< YUV matrix id (BT.709 = 0).

    /// An unknown content identity is never eligible for reuse.
    [[nodiscard]] bool usable() const noexcept { return content_digest != 0; }

    [[nodiscard]] bool operator==(const ConversionCacheKey& other) const noexcept {
        return content_digest == other.content_digest
            && width          == other.width
            && height         == other.height
            && pix_fmt        == other.pix_fmt
            && apply_gamma    == other.apply_gamma
            && color_matrix   == other.color_matrix;
    }

    [[nodiscard]] bool operator!=(const ConversionCacheKey& other) const noexcept {
        return !(*this == other);
    }
};

/// Build the cache key for converting @p fb into the encoded parameters.
/// Only the content identity travels from the framebuffer; geometry, format and
/// colour parameters are the caller's, because they describe the destination.
[[nodiscard]] inline ConversionCacheKey make_conversion_cache_key(
    const Framebuffer& fb,
    int                width,
    int                height,
    int                pix_fmt,
    bool               apply_gamma,
    int                color_matrix) noexcept {
    return ConversionCacheKey{
        .content_digest = fb.content_digest(),
        .width          = width,
        .height         = height,
        .pix_fmt        = pix_fmt,
        .apply_gamma    = apply_gamma,
        .color_matrix   = color_matrix,
    };
}

/// May @p fb contribute reusable converted bytes at all? Both cache consumers
/// (this key and the shared byte cache in `frame_conversion_service.cpp`) ask
/// this one question, so "unknown identity ⇒ convert" cannot diverge.
[[nodiscard]] inline bool has_content_identity(const Framebuffer& fb) noexcept {
    return fb.content_digest() != 0;
}

} // namespace chronon3d::video
