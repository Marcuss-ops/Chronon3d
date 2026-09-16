# Consolidated animation and timeline test registration.

if(CHRONON3D_USE_BLEND2D AND CHRONON3D_ENABLE_TEXT)
    set(_animation_test_sources
        text/test_video_animation_curves.cpp
    )
    if(CHRONON3D_BUILD_CONTENT)
        list(APPEND _animation_test_sources
            animation/test_background_compositions.cpp
        )
    endif()

    chronon3d_add_test_suite(
        NAME chronon3d_animation_tests
        TIER UNIT
        LINK_TARGETS chronon3d_sdk chronon3d_sdk_impl chronon3d_pipeline
        SOURCES ${_animation_test_sources}
    )
    if(CHRONON3D_BUILD_CONTENT)
        target_link_libraries(chronon3d_animation_tests PRIVATE chronon3d_content)
        target_compile_definitions(chronon3d_animation_tests PRIVATE
            CHRONON3D_HAS_CONTENT_MINIMALIST
            CHRONON3D_HAS_CONTENT_BACKGROUNDS
        )
    endif()
endif()

chronon3d_add_test_suite(
    NAME chronon3d_animation_helpers_tests
    TIER UNIT
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/animation/test_animation_helpers.cpp
)

chronon3d_add_test_suite(
    NAME chronon3d_timeline_tests
    TIER UNIT
    SOURCES
        timeline/test_render_job_video.cpp
        timeline/test_composition_descriptor_decode.cpp
        timeline/test_props_codec.cpp
        timeline/test_composition_descriptor_prepare.cpp
        timeline/test_registry_resolve.cpp
        timeline/test_compile_evaluate_dynamic.cpp
)
