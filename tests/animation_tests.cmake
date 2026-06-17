# ── Animation Catalog Tests ──

add_executable(chronon3d_animation_tests
    ${TEST_MAIN}
    animations/test_background_catalog.cpp
)
target_link_libraries(chronon3d_animation_tests PRIVATE chronon3d_pipeline doctest::doctest)
target_include_directories(chronon3d_animation_tests PRIVATE ${CMAKE_SOURCE_DIR})
# WHOLE_ARCHIVE removed: content targets are now small and explicitly registered
if(CHRONON3D_BUILD_CONTENT)
    target_link_libraries(chronon3d_animation_tests PRIVATE chronon3d_all_content_modules)
    target_sources(chronon3d_animation_tests PRIVATE ${CMAKE_SOURCE_DIR}/content/register_content_modules.cpp)
    target_include_directories(chronon3d_animation_tests PRIVATE ${CMAKE_SOURCE_DIR})
    target_compile_definitions(chronon3d_animation_tests PRIVATE CHRONON3D_HAS_CONTENT_MINIMALIST)
endif()
target_include_directories(chronon3d_animation_tests PRIVATE ${CMAKE_SOURCE_DIR})
chronon3d_enable_test_pch(chronon3d_animation_tests)
add_test(NAME chronon3d_animation_tests COMMAND chronon3d_animation_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
