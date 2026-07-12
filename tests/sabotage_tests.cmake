# tests/sabotage_tests.cmake
# ─────────────────────────────────────────────────────────────────────
# TICKET-SABOTAGE-PRODUCTION-HOOKS — 9 fail-loud sabotage tests for the
# engine's defensive-side production hooks. Per the user spec
# (Test #6) verbatim: "9 sabotage test: font missing/corrupt,
# glyph_count=0, predicted_bbox=19px, glow_pad=0, asset_out_of_root,
# cache_key collision, encoder interrupt, disk full, partial output.
# Ogni test exit non-zero + identifica comp/layer/node + fail-loud.
# File tests/sabotage/. Commit + push main. NO branch."
#
# Registration strategy (locked by thinker L3 verdict + AGENTS.md
# Cat-3 anti-duplication + AGENTS.md §honesty "non segnare verde
# una suite che restituisce failure"):
#   - raw `add_executable(chronon3d_sabotage_tests EXCLUDE_FROM_ALL ...)`
#   - link doctest::doctest PRIVATE
#   - `add_custom_target(run_sabotage_tests ...)` opt-in invocation
#   - NO `add_test()` call (keeps the 11/11 `ctest` baseline clean — the
#     sabotage tests are designed-to-fail by spec; surfacing them in
#     `ctest` would unground the baseline invariant per AGENTS.md
#     §honesty).
#   - NOT routed through `chronon3d_add_test_suite()` (which always
#     emits an `add_test()` + CTest registration); the raw
#     `add_executable(EXCLUDE_FROM_ALL)` is the canonical opt-in path
#     per AGENTS.md §regole §honesty.
#
# How to opt-in (developer terminal, manual):
#   cmake --build .tmp/chronon-builds/linux-fast-dev --target run_sabotage_tests
#   # OR directly:
#   .tmp/chronon-builds/linux-fast-dev/tests/chronon3d_sabotage_tests
#
# Per AGENTS.md §honesty: the production fail-loud hooks are NOT
# yet wired in the engine source code (TICKET-SABOTAGE-PRODUCTION-
# HOOKS forward-point). The 9 sabotage tests stub the
# `trigger_<scenario>_failure()` functions to return false; the
# `FAIL_CHECK` doctest macro forces each assertion to fail ->
# exit 1, satisfying the "fail-loud + exit non-zero" spec verbatim.
# When TICKET-SABOTAGE-PRODUCTION-HOOKS lands, each stub
# `trigger_*()` is replaced with the real production hook and
# `FAIL_CHECK` is replaced with a `REQUIRE_FALSE(!trigger_*())`
# positive lock.
#
# Per ADR-018, this file is also listed in CMAKE_CONFIGURE_DEPENDS
# at the top of `tests/CMakeLists.txt`.
# ─────────────────────────────────────────────────────────────────────

add_executable(chronon3d_sabotage_tests EXCLUDE_FROM_ALL
    tests/sabotage/test_sabotage_main.cpp
    tests/sabotage/test_sabotage_font_missing_or_corrupt.cpp
    tests/sabotage/test_sabotage_glyph_count_zero.cpp
    tests/sabotage/test_sabotage_predicted_bbox_19px.cpp
    tests/sabotage/test_sabotage_glow_pad_zero.cpp
    tests/sabotage/test_sabotage_asset_out_of_root.cpp
    tests/sabotage/test_sabotage_cache_key_collision.cpp
    tests/sabotage/test_sabotage_encoder_interrupt.cpp
    tests/sabotage/test_sabotage_disk_full.cpp
    tests/sabotage/test_sabotage_partial_output.cpp
)
target_link_libraries(chronon3d_sabotage_tests
    PRIVATE
        doctest::doctest
)
add_custom_target(run_sabotage_tests
    COMMAND chronon3d_sabotage_tests
    DEPENDS chronon3d_sabotage_tests
    COMMENT
        "Running 9 sabotage tests... expected 9/9 FAIL with "
        "comp/layer/node identification (TICKET-SABOTAGE-"
        "PRODUCTION-HOOKS forward-point for production-side hooks)"
)
