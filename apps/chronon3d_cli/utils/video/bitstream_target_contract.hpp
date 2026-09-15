#pragma once

// =============================================================================
// bitstream_target_contract.hpp — WHO produced the target bitstream contract.
//
// The GOP splice gate compares a SOURCE bitstream against the stream the
// OUTPUT container will carry. This header owns the identity of the target
// side, and nothing else, so the encoder layer can describe its own output
// without depending on the GOP copy module (and vice versa):
//
//   Encoder  the only legal producer whenever a re-encode participates in the
//            output (hybrid / boundary re-encode): the target must describe
//            what the encoder will emit, never what the source happens to
//            contain.
//   Remux    the no-encoder lane: the output container literally carries the
//            source stream, so the source's own codec parameters ARE the
//            output contract. Legal only when no encoder participates.
//
// Keeping the two producers distinct is what stops the splice gate from
// degenerating into `source vs source`.
// =============================================================================

#include <cstdint>
#include <vector>

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
}
#endif

namespace chronon3d::cli {

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
using BitstreamCodecId = AVCodecID;
using BitstreamPixelFormat = AVPixelFormat;
#else
enum class BitstreamCodecId : int {};
enum class BitstreamPixelFormat : int {};
#endif

/// Who produced a BitstreamTargetContract.
enum class BitstreamTargetProducer : std::uint8_t {
    Encoder,
    Remux,
};

/// Bitstream contract used as the single target authority for source
/// inspection and GOP splice validation.
struct BitstreamTargetContract {
    BitstreamTargetProducer producer{BitstreamTargetProducer::Encoder};
    /// True only when the ENCODER itself published the stream this contract
    /// describes: codec, dimensions and its own configuration record. It can
    /// come from the opened AVCodecContext (the encoder states its parameter
    /// sets at open) or from a produced packet that carries them in-band, and
    /// the rule is the same for both producers because they describe the same
    /// thing.
    ///
    /// A contract built from CONFIGURATION ONLY (what the export asked for,
    /// before an encoder ran) is never materialized: it is honest about what it
    /// does not know yet and can never certify a splice on its own.
    bool encoder_materialized{false};
    BitstreamCodecId codec{static_cast<BitstreamCodecId>(0)};
    int profile{-1};
    int level{-1};
    std::uint32_t width{0};
    std::uint32_t height{0};
    BitstreamPixelFormat pixel_format{static_cast<BitstreamPixelFormat>(-1)};
    std::vector<std::uint8_t> parameter_sets{};
#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
    AVColorRange color_range{ AVCOL_RANGE_UNSPECIFIED };
    AVColorSpace color_space{ AVCOL_SPC_UNSPECIFIED };
    AVColorPrimaries color_primaries{ AVCOL_PRI_UNSPECIFIED };
    AVColorTransferCharacteristic color_trc{ AVCOL_TRC_UNSPECIFIED };
#endif

    [[nodiscard]] bool valid() const noexcept {
        return width > 0 && height > 0 && profile >= 0 && level >= 0;
    }
};

} // namespace chronon3d::cli
