# ── Animation Catalog Tests ──

add_executable(chronon3d_animation_tests
    ${TEST_MAIN}
    animations/test_background_catalog.cpp
)
target_link_libraries(chronon3d_animation_tests PRIVATE chronon3d_pipeline doctest::doctest)
target_include_directories(chronon3d_animation_tests PRIVATE ${CMAKE_SOURCE_DIR})
# WHOLE_ARCHIVE removed: content targets are now small and explicitly registered
if(CHRONON3D_BUILD_CONTENT)
    # chronon3d_content_registry transitively PUBLIC-links the gallery,
    # so a single link covers both `register_content_modules()` AND the
    # composition symbols.  Adding register_content_modules.cpp via
    # target_sources would create a duplicate symbol conflict.
    target_link_libraries(chronon3d_animation_tests PRIVATE chronon3d_content_registry)
    target_include_directories(chronon3d_animation_tests PRIVATE ${CMAKE_SOURCE_DIR})
    target_compile_definitions(chronon3d_animation_tests PRIVATE CHRONON3D_HAS_CONTENT_MINIMALIST)
endif()
target_include_directories(chronon3d_animation_tests PRIVATE ${CMAKE_SOURCE_DIR})
chronon3d_enable_test_pch(chronon3d_animation_tests)
add_test(NAME chronon3d_animation_tests COMMAND chronon3d_animation_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
