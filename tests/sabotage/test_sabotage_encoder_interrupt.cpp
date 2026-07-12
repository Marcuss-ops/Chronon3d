// ============================================================================
// tests/sabotage/test_sabotage_encoder_interrupt.cpp
// ============================================================================
//
// Sabotage scenario #7: video encoder thread received a POSIX
// signal (SIGINT / SIGTERM / SIGPIPE) MID-FRAME while writing the
// encoded output to disk. The encoder MUST surface an explicit
// `EncoderInterrupted` Result<> error rather than (a) silently
// truncating the partial frame (which corrupts downstream playback
// at the frame boundary) or (b) silently ignoring the signal (which
// would let a `kill -9` cycle leave zombie encoder threads).
//
// Engine signature (verified-real surfaces): the encoder class is
// `NativeEncoder` (NOT `VideoEncoder` — fabricated class name
// REMOVED), declared at `include/chronon3d/video/native_encoder.hpp`
// (machine-verified via grep 2026-07-12; the fabricated `Video
// Encoder::run_encode_loop()` claim is REPLACED). The canonical
// SIGINT/SIGTERM/SIGPIPE pattern handler is
// `include/chronon3d/core/cancellation_token.hpp` (also verified-
// real, intersection point with the mid-frame interrupt path).
// The Phase 2 fail-loud HOOK signature is PLANNED
// (TICKET-SABOTAGE-PRODUCTION-HOOKS): the production hook will
// wire `NativeEncoder::encode(...)` + `cancellation_token::
// is_cancelled()` to emit `EncoderInterrupted` Result<> on
// mid-frame signal.
//
// Expected fail-loud path: EncoderInterrupted Result<> error +
// the encoder MUST atomically truncate the output file header
// + emit the partial-output FFmpeg-style short-PNG sigil so
// downstream playback fails-Loud at the I/O layer.
//
// Per user spec "Ogni test exit non-zero + identifica comp/layer/node
// + fail-loud":
//   - INFO lines identify comp/layer/node (stub labels).
//   - FAIL_CHECK forces the doctest assertion to fail -> exit 1.
// ============================================================================
#include <doctest/doctest.h>

namespace chaos::sabotage::encoder_interrupt {

// Engine-side trigger fingerprint. Implementation forward-point:
// TICKET-SABOTAGE-PRODUCTION-HOOKS — the production hook will wire
// the verified-real `NativeEncoder::encode(...)` (header at
// include/chronon3d/video/native_encoder.hpp) + the verified-real
// `cancellation_token::is_cancelled()` (header at include/
// chronon3d/core/cancellation_token.hpp) to emit EncoderInterrupted.
bool trigger_encoder_interrupt_failure() {
    return false;
}

} // namespace chaos::sabotage::encoder_interrupt

TEST_CASE(
    "sabotage/encoder_interrupt "
    "[comp=NativeEncoder, layer=EncoderSIGINT, "
    "node=NATIVE_ENCODER_encode_SIGNAL_CAUGHT_MID_FRAME]"
) {
    INFO("Comp=NativeEncoder [verified-real at "
         "include/chronon3d/video/native_encoder.hpp]")
    INFO("Layer=EncoderSIGINT [intersection with "
         "cancellation_token.hpp]")
    INFO("Node=NativeEncoder::encode() caught SIGINT/SIGTERM/"
         "SIGPIPE mid-frame [PLANNED "
         "TICKET-SABOTAGE-PRODUCTION-HOOKS]")
    INFO("Sabotage scenario: NativeEncoder::encode() caught "
         "SIGINT/SIGTERM/SIGPIPE mid-frame -> EncoderInterrupted")
    INFO("Forward-point: TICKET-SABOTAGE-PRODUCTION-HOOKS")
    FAIL_CHECK(
        "sabotage/encoder_interrupt: trigger returns false -- "
        "production fail-loud hook not yet wired "
        "(TICKET-SABOTAGE-PRODUCTION-HOOKS forward-point)"
    );
}
