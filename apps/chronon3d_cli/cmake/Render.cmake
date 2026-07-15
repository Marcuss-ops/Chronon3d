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
    utils/job/render_job.cpp
    utils/job/render_job_setup.cpp
    utils/job/render_job_finalize.cpp
    utils/job/render_job_execute.cpp
    utils/job/render_job_loop.cpp
    utils/job/render_job_write_frame.cpp
    utils/job/cli_render_utils.cpp
    utils/job/report/render_job_report.cpp
)

target_include_directories(chronon3d_cli_render PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli
)
target_link_libraries(chronon3d_cli_render PRIVATE
    chronon3d_cli_core
    chronon3d_pipeline
    chronon3d_backend_image
    CLI11::CLI11
    spdlog::spdlog_header_only
    fmt::fmt
)

target_compile_definitions(chronon3d_cli_core PRIVATE
    CHRONON3D_HAS_CLI_RENDER
)
