# ─── M1.8 §4B TICKET-SIMPLICITY-INSPECT-TEXT tests (chronon3d_inspect_text_tests) ───
#
# Adds the §4B inspect-text test target. This test target covers:
#   1. PASS case (nominal composition, all invariants hold) → exit 0
#   2. FAIL case (composition not found) → exit 1
#   3. JSON output is a valid array
#   4. JSON array has at least one entry
#   5. Non-diagnostic build → error JSON, exit 1
#   6. Frame number appears in JSON output
#
# Per ADR-018, this .cmake file is also listed in the parent's
# CMAKE_CONFIGURE_DEPENDS so Ninja / Make re-run cmake when this file
# is touched.
#
# IMPORTANT: this test target is CONDITIONAL on CHRONON3D_BUILD_CLI_DEV
# (the inspect-text subcommand lives in the chronon3d_cli_dev sub-target,
# which is OFF by default per the CLI build options).  Additionally, the
# diagnostic tests are CONDITIONAL on CHRONON3D_BUILD_DIAGNOSTICS (the
# audit function is gated).  In non-diagnostic builds, only the
# non-diagnostic TEST_CASEs run; in non-CLI-DEV builds, no test target
# is created.

if(NOT CHRONON3D_BUILD_CLI_DEV)
    return()
endif()

chronon3d_add_test_suite(
    NAME chronon3d_inspect_text_tests
    TIER UNIT
    LINK_TARGETS chronon3d_cli_dev
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/cli/test_inspect_text.cpp
)

# Wire into the FAST test aggregator (math + harness, no rendering
# backend required).  Gated by CHRONON3D_BUILD_CLI_DEV via the early
# return above; only append the target to the FAST aggregator when the
# executable exists.
list(APPEND CHRONON3D_FAST_TEST_DEPS chronon3d_inspect_text_tests)

doctest_discover_tests(chronon3d_inspect_text_tests
    PROPERTIES
        TIMEOUT 30
)
