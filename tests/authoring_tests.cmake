# ── Authoring DSL Tests ─────────────────────────────────────────────────
# Equivalence tests for the fluent authoring façade
# (chronon3d::authoring::Animator / Selector). Pure header-only logic,
# no render backend / asset I/O — fits in the fast-tests bucket.

add_executable(chronon3d_authoring_tests
    ${TEST_MAIN}
    authoring/test_animator_dsl.cpp
)
target_link_libraries(chronon3d_authoring_tests
    PRIVATE
        chronon3d_sdk
        chronon3d_sdk_impl
        chronon3d_pipeline
        doctest::doctest
)
# Same guard as core_tests.cmake: authoring tests share test_main.cpp
# which conditionally links against content/extension/text symbols.
if(CHRONON3D_ENABLE_TEXT AND CHRONON3D_USE_BLEND2D AND TARGET chronon3d_backend_text)
    target_link_libraries(chronon3d_authoring_tests PRIVATE chronon3d_backend_text)
endif()
target_include_directories(chronon3d_authoring_tests PRIVATE ${CMAKE_SOURCE_DIR})
# Keep the test-only authoring::testing release accessor visible (the
# `friend struct testing::AnimatorTestAccess` declaration in the
# authoring headers is gated on the test build so it does not leak into
# production SDK).
target_compile_definitions(chronon3d_authoring_tests PRIVATE CHRONON3D_BUILD_TESTS)
chronon3d_enable_test_pch(chronon3d_authoring_tests)
add_test(
    NAME chronon3d_authoring_tests
    COMMAND chronon3d_authoring_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
