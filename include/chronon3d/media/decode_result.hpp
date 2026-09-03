#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/types/time.hpp>
#include <chronon3d/media/decode_diagnostic.hpp>

#include <cstdint>
#include <memory>
#include <variant>

namespace chronon3d::media {

/// A successfully decoded source presentation sample. Source timestamps are
/// retained alongside the renderable framebuffer so decode provenance remains
/// attached to the call result rather than living in mutable provider state.
struct DecodedFrame {
    std::shared_ptr<Framebuffer> framebuffer;
    std::int64_t pts{kNoDecodeTimestamp};
    std::int64_t dts{kNoDecodeTimestamp};
    std::uint64_t source_order{0};
};

/// The requested presentation time is strictly after the source sample range.
/// EOF is a first-class outcome and is never represented as a null framebuffer.
struct DecodeEndOfStream {
    RationalTime requested_time{};
};

/// A decode operation failed for the diagnostic carried by this result.
struct DecodeFailure {
    DecodeDiagnostic diagnostic;
};

using DecodeResult = std::variant<DecodedFrame, DecodeEndOfStream, DecodeFailure>;

[[nodiscard]] inline const DecodedFrame* decoded_frame_if(const DecodeResult& result) noexcept {
    return std::get_if<DecodedFrame>(&result);
}

[[nodiscard]] inline const DecodeFailure* decode_failure_if(const DecodeResult& result) noexcept {
    return std::get_if<DecodeFailure>(&result);
}

[[nodiscard]] inline bool decode_is_eos(const DecodeResult& result) noexcept {
    return std::holds_alternative<DecodeEndOfStream>(result);
}

} // namespace chronon3d::media
