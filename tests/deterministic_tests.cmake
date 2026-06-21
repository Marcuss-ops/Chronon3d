# ── Determinism Tests ──
# Tests for pixel-perfect determinism, semantic comparison, and cache state effects.
# Requires Blend2D for software backend path rendering.

if(CHRONON3D_USE_BLEND2D)
add_executable(chronon3d_deterministic_tests
    ${TEST_MAIN}
    deterministic/test_deterministic.cpp
    deterministic/test_determinism_harness.cpp
    deterministic/gradient_determinism_tests.cpp
    # WP-6 PR 6.1 — TileGrid + DirtyTileMask determinism (pure
    # data structures, no TBB).  Proves the bit-pattern + iteration-
    # order invariants so any future change that introduces scheduler
    # state inside the tile data path fails this gate immediately
    # (the TICKET-007.q/r/s/t/u renderer-level non-determinism is a
    # separate ticket — see docs/FOLLOWUP_TICKETS.md).
    deterministic/test_tile_determinism.cpp
    # WP1 PR 1.4 — Scheduler-swap determinism (Sequential vs
    # TbbFixed/TbbAutomatic across required scenes).  Lives under
    # tests/render_graph/executor/ because the executor's scheduler
    # parameter (WP1 PR 1.0+1.1) is the test surface, but it reuses
    # the determinism harness helpers from this aggregate.
    render_graph/executor/test_scheduler_determinism.cpp
)

target_link_libraries(chronon3d_deterministic_tests
    PRIVATE
        chronon3d_sdk
        chronon3d_backend_software
        chronon3d_scene
        doctest::doctest
)

target_compile_definitions(chronon3d_deterministic_tests PRIVATE CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(chronon3d_deterministic_tests PRIVATE ${CMAKE_SOURCE_DIR})
set_target_properties(chronon3d_deterministic_tests PROPERTIES UNITY_BUILD OFF)
chronon3d_enable_test_pch(chronon3d_deterministic_tests)
add_test(NAME chronon3d_deterministic_tests COMMAND chronon3d_deterministic_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
