// ============================================================================
// tests/text/test_pipeline_parity_video.cpp
//
// TICKET-REFACTOR-TESTS-SPLIT-18-19 §A — pipeline parity: video path.
//
// Tests that the SDK in-process still frame matches the first frame of
// the CLI video render (chronon3d_cli video PipelineParityCanary ...
// --ffmpeg-mode png --keep-frames).  The CLI subprocess uses the same
// canary composition the SDK renders, so the stills must be byte-exact.
//
// This file contains 1 TEST_CASE:
//   - "real pipeline parity: SDK still == raw video frame"
//
// SKIP-rot guard: if ffmpeg is not on PATH the test is SKIPped (per
// TICKET-DOCTEST-SKIP-ROT) rather than failing — the parity matrix is
// only meaningful in a working-build host with ffmpeg installed.
// ============================================================================

#include <doctest/doctest.h>
#include <tests/helpers/doctest_skip_compat.hpp>

#include <tests/text/support/pipeline_parity_harness.hpp>

namespace ph = chronon3d::test::parity_harness;

// ═══════════════════════════════════════════════════════════════════════════
// Test: SDK still == raw video frame
// ═══════════════════════════════════════════════════════════════════════════
TEST_CASE("real pipeline parity: SDK still == raw video frame") {
    if (!ph::ffmpeg_available()) {
        SKIP("TICKET-DOCTEST-SKIP-ROT: ffmpeg not found in PATH");
    }

    const auto tmp = ph::make_temp_dir();
    const std::string sdk_png = (tmp / "sdk.png").string();

    const auto sdk_hash = ph::render_sdk_to_png(sdk_png, ph::default_settings());
    const std::string video_frame = ph::run_cli_video_first_frame(tmp / "video", "");
    REQUIRE(!video_frame.empty());
    const auto video_hash = ph::hash_from_png(video_frame);

    CHECK(sdk_hash == video_hash);
}
