# ── Video Contracts Tests — canonical video integration coverage ──
#
# Per-area early-return gate (TICKET-CMAKE-TEST-MANIFEST-UNIFICATION).
if(NOT CHRONON3D_ENABLE_VIDEO)
    return()
endif()

set(_video_contracts_link_targets
    chronon3d_cli_render chronon3d_cli_core
    chronon3d_sdk chronon3d_sdk_impl chronon3d_pipeline
    chronon3d_scene chronon3d_backend_software
    chronon3d_media_video chronon3d_backend_image
    CLI11::CLI11 fmt::fmt)
if(TARGET chronon3d_cli_dev)
    list(APPEND _video_contracts_link_targets chronon3d_cli_dev)
endif()

chronon3d_add_test_suite(
    NAME chronon3d_video_contracts_tests
    TIER INTEGRATION
    LINK_TARGETS ${_video_contracts_link_targets}
    SOURCES video/test_video_contracts.cpp
)
target_include_directories(chronon3d_video_contracts_tests
    PRIVATE ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli)

if(CHRONON3D_ENABLE_NATIVE_FFMPEG AND TARGET chronon3d_media_native)
    chronon3d_add_test_suite(
        NAME chronon3d_native_video_sink_factory_tests
        TIER INTEGRATION
        LINK_TARGETS chronon3d_media_video chronon3d_media_native
        SOURCES video/test_native_av_sink_factory.cpp
                video/test_native_av_sink_lifecycle.cpp
    )
else()
    # Regression lock for the demolished subprocess fallback: raw output stays
    # available, while compressed output has no hidden process-based authority.
    chronon3d_add_test_suite(
        NAME chronon3d_no_native_video_sink_factory_tests
        TIER UNIT
        LINK_TARGETS chronon3d_media_video
        SOURCES video/test_video_sink_factory_no_native.cpp
    )
endif()
