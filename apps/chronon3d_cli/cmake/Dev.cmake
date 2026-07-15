if(NOT CHRONON3D_BUILD_CLI_DEV)
    return()
endif()

if(NOT TARGET chronon3d_cli_render)
    message(FATAL_ERROR
        "CHRONON3D_BUILD_CLI_DEV requires CHRONON3D_BUILD_CLI_RENDER")
endif()

add_library(chronon3d_cli_dev STATIC
    commands/group_dev.cpp
    commands/dev/register_dev_commands.cpp
    commands/dev/register_inspect_commands.cpp
    commands/dev/command_batch.cpp
    commands/dev/command_bench_convert.cpp
    commands/dev/command_camera_path.cpp
    commands/dev/command_inspect_text.cpp
    commands/dev/command_text_def_inspect.cpp
    # Phase 1d / Increment C — TICKET-PHASE1D-V2-REGISTRY-INTROSPECTION
    commands/dev/command_schema.cpp
    $<$<AND:$<BOOL:${CHRONON3D_USE_BLEND2D}>,$<BOOL:${CHRONON3D_ENABLE_TEXT}>>:commands/dev/text_audit_helpers.cpp>
    $<$<AND:$<BOOL:${CHRONON3D_USE_BLEND2D}>,$<BOOL:${CHRONON3D_ENABLE_TEXT}>>:commands/dev/text_inspection_collector.cpp>
    utils/batch/batch_job_spec.cpp
    utils/batch/batch_runner.cpp
    ${CMAKE_SOURCE_DIR}/content/ae_parity/ae_cam_scenes.cpp
    ${CMAKE_SOURCE_DIR}/tests/visual/camera_truth/camera_truth_test.cpp
    ${CMAKE_SOURCE_DIR}/tests/visual/camera_truth/camera_truth_orbit.cpp
    ${CMAKE_SOURCE_DIR}/tests/visual/ae_parity/ae_parity_compositions.cpp
    ${CMAKE_SOURCE_DIR}/tests/visual/glow_ab/glow_ab_compositions.cpp
)
target_include_directories(chronon3d_cli_dev PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli
)
target_link_libraries(chronon3d_cli_dev PRIVATE
    chronon3d_cli_core
    chronon3d_cli_render
    chronon3d_pipeline
    CLI11::CLI11
    spdlog::spdlog_header_only
    fmt::fmt
)
