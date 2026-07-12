// ============================================================================
// tests/text/test_pipeline_parity_runtime_modes.cpp
//
// TICKET-REFACTOR-TESTS-SPLIT-18-19 §A — pipeline parity: runtime modes.
//
// 7 TEST_CASEs that exercise the SDK ↔ CLI parity for runtime-mode flags:
//
//   1. pruning OFF            (SDK dirty.enabled=false == CLI --no-dirty-rects)
//   2. 1 thread               (SDK tbb::global_control == CLI CHRONON3D_THREADS=1)
//   3. warm/cold cache        (fresh vs reused renderer / --warmup-renderer)
//   4. modular graph OFF      (SDK use_modular_graph=false == CLI --no-graph)
//   5. modular graph ON       (SDK use_modular_graph=true == CLI --graph)
//   6. diagnostic overlay ON  (SDK text_layout_debug == CLI --diagnostic-overlay)
//   7. diagnostic overlay ONLY (same + diagnostic_overlay_only)
//
// Each test compares a u64 hash of the SDK-rendered PNG with a u64 hash
// of the CLI-subprocess-rendered PNG; byte-exact equality == parity.
// ============================================================================

#include <doctest/doctest.h>
#include <tests/helpers/doctest_skip_compat.hpp>

#include <tests/text/support/pipeline_parity_harness.hpp>

#include <tbb/global_control.h>

namespace ph = chronon3d::test::parity_harness;

// ═══════════════════════════════════════════════════════════════════════════
// Test: pruning OFF parity (SDK dirty.enabled=false == CLI --no-dirty-rects)
// ═══════════════════════════════════════════════════════════════════════════
TEST_CASE("real pipeline parity: pruning OFF") {
    const auto tmp = ph::make_temp_dir();
    const std::string sdk_png = (tmp / "sdk_prune.png").string();
    const std::string cli_png = (tmp / "cli_prune.png").string();

    auto settings = ph::default_settings();
    settings.dirty.enabled = false;
    settings.dirty.use_bitmask = false;
    settings.dirty.use_tiles = false;

    const auto sdk_hash = ph::render_sdk_to_png(sdk_png, settings);
    REQUIRE(ph::run_cli_still(cli_png, "--no-dirty-rects"));
    const auto cli_hash = ph::hash_from_png(cli_png);

    CHECK(sdk_hash == cli_hash);
}

// ═══════════════════════════════════════════════════════════════════════════
// Test: 1/N thread parity
// ═══════════════════════════════════════════════════════════════════════════
TEST_CASE("real pipeline parity: 1 thread") {
    const auto tmp = ph::make_temp_dir();
    const std::string sdk_png = (tmp / "sdk_1t.png").string();
    const std::string cli_png = (tmp / "cli_1t.png").string();

    std::uint64_t sdk_hash = 0;
    {
        tbb::global_control tbb_ctrl(
            tbb::global_control::max_allowed_parallelism, 1);
        sdk_hash = ph::render_sdk_to_png(sdk_png, ph::default_settings());
    }

    REQUIRE(ph::run_cli_still(cli_png, "", "CHRONON3D_THREADS=1"));
    const auto cli_hash = ph::hash_from_png(cli_png);

    CHECK(sdk_hash == cli_hash);
}

// ═══════════════════════════════════════════════════════════════════════════
// Test: warm/cold cache parity
//   - SDK cold: fresh renderer, first render.
//   - SDK warm: same renderer, second render.
//   - CLI cold: no warmup flags.
//   - CLI warm: --warmup-renderer --warmup-dummy-frame.
// All four hashes must be identical.
// ═══════════════════════════════════════════════════════════════════════════
TEST_CASE("real pipeline parity: warm/cold cache") {
    const auto tmp = ph::make_temp_dir();

    const std::string sdk_cold_png = (tmp / "sdk_cold.png").string();
    const std::string sdk_warm_png = (tmp / "sdk_warm.png").string();
    const std::string cli_cold_png = (tmp / "cli_cold.png").string();
    const std::string cli_warm_png = (tmp / "cli_warm.png").string();

    // SDK cold
    const auto sdk_cold_hash = ph::render_sdk_to_png(sdk_cold_png, ph::default_settings());

    // SDK warm: reuse the same renderer (caches populated by the cold render).
    const auto sdk_warm_hash = ph::render_sdk_to_png(sdk_warm_png, ph::default_settings(), /*warm=*/true);

    // CLI cold
    REQUIRE(ph::run_cli_still(cli_cold_png, ""));
    const auto cli_cold_hash = ph::hash_from_png(cli_cold_png);

    // CLI warm
    REQUIRE(ph::run_cli_still(cli_warm_png, "--warmup-renderer --warmup-dummy-frame"));
    const auto cli_warm_hash = ph::hash_from_png(cli_warm_png);

    CHECK(sdk_cold_hash == sdk_warm_hash);
    CHECK(sdk_cold_hash == cli_cold_hash);
    CHECK(sdk_cold_hash == cli_warm_hash);
}

// ═══════════════════════════════════════════════════════════════════════════
// Test: modular graph OFF parity
//   SDK: RenderSettings.use_modular_graph = false
//   CLI: --no-graph
// ═══════════════════════════════════════════════════════════════════════════
TEST_CASE("real pipeline parity: modular graph OFF") {
    const auto tmp = ph::make_temp_dir();
    const std::string sdk_png = (tmp / "sdk_nograph.png").string();
    const std::string cli_png = (tmp / "cli_nograph.png").string();

    auto settings = ph::default_settings();
    settings.use_modular_graph = false;

    const auto sdk_hash = ph::render_sdk_to_png(sdk_png, settings);
    REQUIRE(ph::run_cli_still(cli_png, "--no-graph"));
    const auto cli_hash = ph::hash_from_png(cli_png);

    CHECK(sdk_hash == cli_hash);
}

// ═══════════════════════════════════════════════════════════════════════════
// Test: modular graph ON (explicit) parity
//   SDK: RenderSettings.use_modular_graph = true
//   CLI: --graph
// ═══════════════════════════════════════════════════════════════════════════
TEST_CASE("real pipeline parity: modular graph ON (explicit)") {
    const auto tmp = ph::make_temp_dir();
    const std::string sdk_png = (tmp / "sdk_graph.png").string();
    const std::string cli_png = (tmp / "cli_graph.png").string();

    auto settings = ph::default_settings();
    settings.use_modular_graph = true;

    const auto sdk_hash = ph::render_sdk_to_png(sdk_png, settings);
    REQUIRE(ph::run_cli_still(cli_png, "--graph"));
    const auto cli_hash = ph::hash_from_png(cli_png);

    CHECK(sdk_hash == cli_hash);
}

// ═══════════════════════════════════════════════════════════════════════════
// Test: diagnostic overlay ON parity
//   SDK: settings.text_layout_debug = true
//   CLI: --diagnostic-overlay
// ═══════════════════════════════════════════════════════════════════════════
TEST_CASE("real pipeline parity: diagnostic overlay ON") {
    const auto tmp = ph::make_temp_dir();
    const std::string sdk_png = (tmp / "sdk_diag.png").string();
    const std::string cli_png = (tmp / "cli_diag.png").string();

    auto settings = ph::default_settings();
    settings.text_layout_debug = true;

    const auto sdk_hash = ph::render_sdk_to_png(sdk_png, settings);
    REQUIRE(ph::run_cli_still(cli_png, "--diagnostic-overlay"));
    const auto cli_hash = ph::hash_from_png(cli_png);

    CHECK(sdk_hash == cli_hash);
}

// ═══════════════════════════════════════════════════════════════════════════
// Test: diagnostic overlay ONLY parity
//   SDK: settings.text_layout_debug = true AND
//        settings.diagnostic_overlay_only = true
//   CLI: --diagnostic-overlay-only
// ═══════════════════════════════════════════════════════════════════════════
TEST_CASE("real pipeline parity: diagnostic overlay ONLY") {
    const auto tmp = ph::make_temp_dir();
    const std::string sdk_png = (tmp / "sdk_diag_only.png").string();
    const std::string cli_png = (tmp / "cli_diag_only.png").string();

    auto settings = ph::default_settings();
    settings.text_layout_debug = true;
    settings.diagnostic_overlay_only = true;

    const auto sdk_hash = ph::render_sdk_to_png(sdk_png, settings);
    REQUIRE(ph::run_cli_still(cli_png, "--diagnostic-overlay-only"));
    const auto cli_hash = ph::hash_from_png(cli_png);

    CHECK(sdk_hash == cli_hash);
}
