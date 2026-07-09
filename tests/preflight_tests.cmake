# tests/preflight_tests.cmake
#
# Registers the preflight cache contract test target. Kept separate from
# the per-area `.cmake` files so the new `PathExistenceMap` test surface
# can evolve without touching scene/cli/cache orchestration files.

chronon3d_add_test_suite(
    NAME chronon3d_preflight_tests
    TIER SDK
    LINK_TARGETS chronon3d_core chronon3d_runtime chronon3d_assets
    SOURCES preflight/test_path_existence_map.cpp
)
