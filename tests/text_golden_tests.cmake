# ═══════════════════════════════════════════════════════════════════════════
# tests/text_golden_tests.cmake — TXT-QA-01 Real Golden Text Harness
#
# Standalone executable for the text golden regression test suite.
# Kept separate from chronon3d_text_preset_visual_tests to avoid
# unity-build conflicts between golden_test.cpp / image_diff.cpp
# and the text visual harness includes.
#
# Golden PNGs:   test_renders/golden/text/
# Artifacts:     test_renders/artifacts/text/
#
# Update goldens: CHRONON3D_UPDATE_GOLDENS=1 ctest -R TextGolden
# ═══════════════════════════════════════════════════════════════════════════

add_executable(chronon3d_text_golden_tests
    ${TEST_MAIN}
    text/test_text_golden.cpp
    visual/support/golden_test.cpp
    visual/support/image_diff.cpp
)

target_link_libraries(chronon3d_text_golden_tests
    PRIVATE
        chronon3d_sdk
        chronon3d_software
        doctest::doctest
)

target_compile_definitions(chronon3d_text_golden_tests PRIVATE CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(chronon3d_text_golden_tests PRIVATE ${CMAKE_SOURCE_DIR})
set_target_properties(chronon3d_text_golden_tests PROPERTIES UNITY_BUILD OFF)
chronon3d_enable_test_pch(chronon3d_text_golden_tests)
add_test(
    NAME TextGolden
    COMMAND chronon3d_text_golden_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
