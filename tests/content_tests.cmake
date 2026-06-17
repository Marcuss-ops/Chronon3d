# ── Content Module Tests (registration contracts + composition smoke) ──

add_executable(chronon3d_content_tests
    ${TEST_MAIN}
    content/test_content_module_contract.cpp
    content/test_content_composition_smoke.cpp
)
target_link_libraries(chronon3d_content_tests
    PRIVATE
        chronon3d_pipeline
        chronon3d_extension
        chronon3d_scene
        chronon3d_backend_software
        doctest::doctest
)
target_include_directories(chronon3d_content_tests PRIVATE ${CMAKE_SOURCE_DIR})
# WHOLE_ARCHIVE removed — content uses explicit ExtensionRegistry registration
if(CHRONON3D_BUILD_CONTENT)
    target_link_libraries(chronon3d_content_tests PRIVATE chronon3d_all_content_modules)
    target_sources(chronon3d_content_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/content/register_content_modules.cpp
    )
    target_include_directories(chronon3d_content_tests PRIVATE ${CMAKE_SOURCE_DIR})
    target_compile_definitions(chronon3d_content_tests PRIVATE CHRONON3D_HAS_CONTENT_MINIMALIST CHRONON3D_HAS_CONTENT_2D5)
endif()
chronon3d_enable_test_pch(chronon3d_content_tests)
add_test(NAME chronon3d_content_tests COMMAND chronon3d_content_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
