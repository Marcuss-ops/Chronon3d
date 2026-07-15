# Focused canonical RenderJob planner/executor contract.
# Built independently from the broad CLI suite so configuration matrices can
# certify video-enabled and image-only variants without compiling unrelated CLI
# commands or developer tools.
if(NOT CHRONON3D_BUILD_CLI OR NOT TARGET chronon3d_cli_render)
    return()
endif()

set(_render_job_contract_links
    chronon3d_cli_render
    chronon3d_cli_core
    chronon3d_sdk
    chronon3d_sdk_impl
    chronon3d_pipeline
    chronon3d_scene
    chronon3d_backend_software
    chronon3d_backend_image
    CLI11::CLI11
    fmt::fmt
)

if(TARGET chronon3d_cli_video_export)
    list(APPEND _render_job_contract_links
        chronon3d_cli_video_export
        chronon3d_media_video
    )
endif()

chronon3d_add_test_suite(
    NAME chronon3d_render_job_contract_tests
    TIER SDK
    LINK_TARGETS ${_render_job_contract_links}
    SOURCES cli/test_render_job_planning.cpp
)

target_include_directories(chronon3d_render_job_contract_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli
)

if(NOT TARGET chronon3d_cli_video_export)
    target_compile_definitions(chronon3d_render_job_contract_tests PRIVATE
        CHRONON3D_TEST_VIDEO_EXPORT_DISABLED=1
    )
endif()
