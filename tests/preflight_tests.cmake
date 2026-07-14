# tests/preflight_tests.cmake
#
# Per-area early-return gate (TICKET-CMAKE-TEST-MANIFEST-UNIFICATION).
if(NOT CHRONON3D_BUILD_TESTS)
    return()
endif()
#
# Registers the preflight cache contract test target. Kept separate from
# the per-area `.cmake` files so the new `PathExistenceMap` test surface
# can evolve without touching scene/cli/cache orchestration files.

chronon3d_add_test_suite(
    NAME chronon3d_preflight_tests
    TIER UNIT
    SOURCES preflight/test_path_existence_map.cpp
)
