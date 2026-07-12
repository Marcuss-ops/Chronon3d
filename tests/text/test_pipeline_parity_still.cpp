// ============================================================================
// tests/text/test_pipeline_parity_still.cpp
//
// TICKET-REFACTOR-TESTS-SPLIT-18-19 §A — pipeline parity: still-only path.
//
// Tests that the SDK in-process SoftwareRenderer produces a byte-exact
// framebuffer for the canary composition as the CLI subprocess
// (chronon3d_cli still PipelineParityCanary ...).  The same composition
// is rendered by every path; outputs are saved as PNG, loaded back into
// a Framebuffer, and hashed with the same helper so comparisons are
// byte-exact when the pipelines are correct.
//
// This file contains 1 TEST_CASE:
//   - "real pipeline parity: SDK still == CLI still"
//
// Sister files (in the same executable):
//   - test_pipeline_parity_video.cpp          (still ↔ video frame)
//   - test_pipeline_parity_runtime_modes.cpp  (cache/thread/graph/diag)
//   - test_chronon_glow_temporal.cpp          (ChrononGlow temporal)
// ============================================================================

#include <doctest/doctest.h>
#include <tests/helpers/doctest_skip_compat.hpp>

#include <tests/text/support/pipeline_parity_harness.hpp>

namespace ph = chronon3d::test::parity_harness;

// ═══════════════════════════════════════════════════════════════════════════
// Test: SDK still == CLI still
// ═══════════════════════════════════════════════════════════════════════════
TEST_CASE("real pipeline parity: SDK still == CLI still") {
    const auto tmp = ph::make_temp_dir();
    const std::string sdk_png = (tmp / "sdk.png").string();
    const std::string cli_png = (tmp / "cli.png").string();

    const auto sdk_hash = ph::render_sdk_to_png(sdk_png, ph::default_settings());
    REQUIRE(ph::run_cli_still(cli_png, ""));
    const auto cli_hash = ph::hash_from_png(cli_png);

    CHECK(sdk_hash == cli_hash);
}
