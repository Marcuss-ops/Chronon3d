if(NOT CHRONON3D_BUILD_CLI_RENDER)
    return()
endif()

add_library(chronon3d_cli_render STATIC
    commands/group_render.cpp
    commands/render/register_render_commands.cpp
    commands/render/command_render.cpp
    commands/render/command_preflight.cpp
    commands/render/command_bake_layer.cpp
    commands/render/command_graph.cpp
    commands/render/command_render_plan.cpp
    commands/render/render_plan_preparation.cpp
    commands/render/render_plan_inspection.cpp
    commands/render/command_inspect_plan.cpp
    commands/render/command_script.cpp
    commands/render/audio_muxer.cpp
    utils/job/render_job.cpp
    utils/job/render_job_setup.cpp
    utils/job/render_job_finalize.cpp
    utils/job/render_job_execute.cpp
    utils/job/render_job_loop.cpp
    utils/job/render_job_write_frame.cpp
    utils/job/cli_render_utils.cpp
    utils/job/report/render_job_report.cpp
    utils/semantic/semantic_script.cpp
)

target_include_directories(chronon3d_cli_render PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli
)
target_link_libraries(chronon3d_cli_render PRIVATE
    chronon3d_cli_core
    chronon3d_render_plan
    chronon3d_render_plan_compiler
    chronon3d_pipeline
    chronon3d_backend_image
    CLI11::CLI11
    spdlog::spdlog_header_only
    fmt::fmt
)

if(CHRONON3D_ENABLE_VIDEO AND TARGET chronon3d_media_video)
    target_link_libraries(chronon3d_cli_render PRIVATE chronon3d_media_video)
endif()

target_compile_definitions(chronon3d_cli_core PRIVATE
    CHRONON3D_HAS_CLI_RENDER
)

target_compile_definitions(chronon3d_cli_render PRIVATE
    CHRONON3D_CLI_PROJECT_VERSION="${PROJECT_VERSION}"
)

if(CHRONON3D_ENABLE_TELEMETRY)
    target_compile_definitions(chronon3d_cli_render PRIVATE
        CHRONON3D_ENABLE_SQLITE_TELEMETRY
    )
endif()
