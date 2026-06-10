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
# WHOLE_ARCHIVE removed: content targets are now small and explicitly registered
if(CHRONON3D_BUILD_CONTENT)
    if(TARGET chronon3d_content_anims)
        target_link_libraries(chronon3d_content_tests PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,chronon3d_content_anims>)
    endif()
    if(TARGET chronon3d_content_minimalist)
        target_link_libraries(chronon3d_content_tests PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,chronon3d_content_minimalist>)
    endif()
    if(TARGET chronon3d_content_text)
        target_link_libraries(chronon3d_content_tests PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,chronon3d_content_text>)
    endif()
    if(TARGET chronon3d_content_2d5)
        target_link_libraries(chronon3d_content_tests PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,chronon3d_content_2d5>)
    endif()
    target_sources(chronon3d_content_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/content/register_minimalist_content.cpp
        ${CMAKE_SOURCE_DIR}/content/register_text_content.cpp
        ${CMAKE_SOURCE_DIR}/content/register_2d5_content.cpp
    )
    target_include_directories(chronon3d_content_tests PRIVATE ${CMAKE_SOURCE_DIR})
    target_compile_definitions(chronon3d_content_tests PRIVATE CHRONON3D_HAS_CONTENT_MINIMALIST CHRONON3D_HAS_CONTENT_TEXT CHRONON3D_HAS_CONTENT_2D5)
endif()
chronon3d_enable_test_pch(chronon3d_content_tests)
add_test(NAME chronon3d_content_tests COMMAND chronon3d_content_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
