#pragma once

// =============================================================================
// encoder_capability.hpp — Typed encoder feed capability contract (Z1)
//
// The zero-copy plan's "one encoder authority, typed capability" principle
// (TICKET-ZERO-COPY-ZWAVES Z1): an encoder session accepts one *kind* of
// frame feed, decided once by the single execution resolver.  A host RGBA
// frame must never reach the native-surface endpoint by accident and a
// DirectYuvFrame must never reach a FullGraph native session.
//
// This header is the backend-neutral declaration of that contract.  The
// runtime authority (`cli::IVideoEncoder` implementations and the CLI
// encoder resolver) enforces it; these types deliberately contain no Vulkan,
// CUDA or NVENC vocabulary.
// =============================================================================

#include <chronon3d/media/video/video_execution_resolver.hpp>

#include <cstdint>
#include <string_view>

namespace chronon3d::media {

/// The single kind of frame payload an encoder endpoint can consume.
/// Deliberately NOT a bitmask of "maybe CPU, maybe CUDA, maybe NVENC":
/// one plan → one feed capability set, chosen by the resolver.
enum class EncoderFeed : std::uint8_t {
    /// Whole-clip packet copy / no render output reaches the encoder.
    None = 0,
    /// CPU RGBA framebuffer feed (`write_frame` / host-frame fallback lane).
    HostFrame = 1,
    /// NV12/P010 decode+overlay feed (`write_direct_yuv`, DirectYuv lane).
    DirectYuv = 2,
    /// GPU surface feed (`write_native_surface`, FullGraph native lane).
    NativeSurface = 3,
};

/// The feed capability set of one encoder session, resolved from the
/// orthogonal `VideoExecutionPlan` axes by `resolve_encoder_capabilities()`.
struct EncoderCapabilitySet {
    bool host_frame{false};
    bool direct_yuv{false};
    bool native_surface{false};

    [[nodiscard]] constexpr bool accepts(EncoderFeed feed) const noexcept {
        switch (feed) {
            case EncoderFeed::HostFrame:     return host_frame;
            case EncoderFeed::DirectYuv:     return direct_yuv;
            case EncoderFeed::NativeSurface: return native_surface;
            case EncoderFeed::None:          return none();
        }
        return false;
    }

    [[nodiscard]] constexpr bool none() const noexcept {
        return !host_frame && !direct_yuv && !native_surface;
    }
};

/// Resolve the encoder feed capability set for a final execution plan.
///
/// Mapping (mirrors the resolver lanes in video_execution_resolver.cpp):
///   - DirectYuv composite            → DirectYuv frames only
///   - NVENC encode (FullGraph GPU)   → native GPU surfaces only
///   - Pipe / software host lanes     → host frames only
///   - Whole-clip packet copy         → no feed (encoder is a muxer only)
///
/// A native NVENC plan never declares host-frame capability: the explicit
/// CPU fallback of a native encoder is a separate, deliberately-requested
/// lane, never part of the plan's contract.
[[nodiscard]] constexpr EncoderCapabilitySet resolve_encoder_capabilities(
    const VideoExecutionPlan& plan) noexcept {
    if (plan.encode == EncodePath::PacketCopy) {
        return EncoderCapabilitySet{};
    }
    if (plan.composite == CompositePath::DirectYuv) {
        return EncoderCapabilitySet{
            .host_frame = false, .direct_yuv = true, .native_surface = false};
    }
    if (plan.encode == EncodePath::Nvenc) {
        return EncoderCapabilitySet{
            .host_frame = false, .direct_yuv = false, .native_surface = true};
    }
    return EncoderCapabilitySet{
        .host_frame = true, .direct_yuv = false, .native_surface = false};
}

/// Stable string-form name for an encoder feed capability. Logging only.
[[nodiscard]] inline std::string_view encoder_feed_name(EncoderFeed feed) noexcept {
    switch (feed) {
        case EncoderFeed::None:          return "none";
        case EncoderFeed::HostFrame:     return "host_frame";
        case EncoderFeed::DirectYuv:     return "direct_yuv";
        case EncoderFeed::NativeSurface: return "native_surface";
    }
    return "unknown";
}

} // namespace chronon3d::media
