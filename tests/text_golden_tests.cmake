# ═══════════════════════════════════════════════════════════════════════════
# tests/text_golden_tests.cmake — TXT-QA-01 Real Golden Text Harness
#
# TICKET-REFACTOR-TESTS-SPLIT-18-19 §B — refactored to pure aggregator.
#
# The test executable (chronon3d_text_golden_tests) is still defined HERE
# (it must live in a single TU, so it cannot be split across the 6
# sub-files).  All target_sources() + add_test() calls have been moved
# to the 6 sub-files in tests/cmake/text/:
#
#   - text_user_spec.cmake   (ambito 1:  12 user-spec golden tests)
#   - text_ae_parity.cmake   (ambiti 2-4: AE parity + glow + motion blur)
#   - text_completeness.cmake(ambiti 5-15: 11 core text quality ambiti)
#   - text_transforms.cmake  (ambito 16: 6 transforms tests)
#   - text_animation.cmake   (ambito 17: 2 animation tests)
#   - text_acceptance.cmake  (multilingual + export acceptance cluster)
#
# Each sub-file does ONLY target_sources(chronon3d_text_golden_tests ...)
# and add_test(NAME ... COMMAND chronon3d_text_golden_tests ...).  The
# aggregator defines the executable + includes the 6 sub-files in order.
#
# Golden PNGs:   test_renders/golden/text/
# Artifacts:     test_renders/artifacts/text/
#
# Update goldens: CHRONON3D_UPDATE_GOLDENS=1 ctest -R TextGolden
# ═══════════════════════════════════════════════════════════════════════════

chronon3d_add_test_suite(
    NAME chronon3d_text_golden_tests
    TIER INTEGRATION
    # TICKET-RENDER-PIPELINE-INTEGRITY M2 fix: chronon3d_software is an
    # INTERFACE that does NOT transitively pull in chronon3d_core (and
    # therefore chronon3d_runtime / RenderRuntime).  The new whitespace
    # test materialises TextRunShape via runtime::RenderRuntime, so we
    # link chronon3d_runtime explicitly.  chronon3d_text_core provides
    # FontEngine.
    LINK_TARGETS chronon3d_sdk chronon3d_software chronon3d_content
                  chronon3d_runtime chronon3d_text_core
    SOURCES text/test_text_golden.cpp
            visual/support/golden_test.cpp
            visual/support/image_diff.cpp
            # TICKET-RENDER-PIPELINE-INTEGRITY layer 3 — whitespace guard
            # short-circuits before compile_text_layout.
            text/test_materialize_whitespace_guarded.cpp
)
target_compile_definitions(chronon3d_text_golden_tests PRIVATE CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# ── Aggregator: include the 6 sub-files (each adds target_sources() + add_test()) ──
include(${CMAKE_CURRENT_LIST_DIR}/cmake/text/text_user_spec.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/text/text_ae_parity.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/text/text_completeness.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/text/text_transforms.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/text/text_animation.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/text/text_acceptance.cmake)
