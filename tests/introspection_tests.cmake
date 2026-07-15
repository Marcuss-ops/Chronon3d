# ─── Phase 1d / TICKET-PHASE1D-V2-REGISTRY-INTROSPECTION tests ─────────────
#
# Member subcommands:
#   - `chronon schema <comp_id>`            — PropsSchema JSON     (Increment C)
#   - `chronon example-props <comp_id>`     — default ValueMap     (Increment D)
#   - `chronon validate <comp_id>`          — decode/validate gate (Increment E)
#   - `chronon resolve <comp_id>`           — ResolvedSpec dump    (Increment F)
#
# Each subcommand owns one test source.  Gated by `CHRONON3D_BUILD_CLI_DEV`
# (the dev subcommand group is OFF by default per the CLI build options).

if(NOT CHRONON3D_BUILD_CLI_DEV)
    return()
endif()

chronon3d_add_test_suite(
    NAME chronon3d_introspection_tests
    TIER UNIT
    LINK_TARGETS chronon3d_cli_dev
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/cli/test_schema_command.cpp
)

# Wire into the FAST test aggregator (no rendering backend required).
# Gated by `CHRONON3D_BUILD_CLI_DEV` via the early return above; only
# append the target to the FAST aggregator when the executable exists.
# Per-tests/inspect_text_tests.cmake:43 precedent (canonical pattern).
list(APPEND CHRONON3D_FAST_TEST_DEPS chronon3d_introspection_tests)
