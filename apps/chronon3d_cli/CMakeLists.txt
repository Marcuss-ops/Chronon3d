add_executable(chronon3d_cli
    main.cpp
    command_registry.cpp
    commands/register_basic_commands.cpp
    commands/register_render_commands.cpp
    commands/register_video_commands.cpp
    commands/register_dev_commands.cpp
    commands/register_inspect_commands.cpp
    commands/register_bench_commands.cpp
    commands/register_telemetry_commands.cpp
    utils/cli_utils.cpp
    utils/cli_render_utils.cpp
    utils/ffmpeg_pipe_encoder.cpp
    utils/render_job.cpp
    utils/batch_job_spec.cpp
    utils/batch_runner.cpp
    utils/watch_service.cpp

    commands/command_list.cpp
    commands/command_info.cpp
    commands/command_render.cpp
    commands/command_studio.cpp
    commands/command_batch.cpp
    commands/command_watch.cpp
    commands/command_video.cpp
    commands/command_doctor_verify.cpp
    commands/command_bench.cpp
    commands/command_graph.cpp
    commands/command_preflight.cpp
    commands/command_proofs.cpp
    commands/command_telemetry.cpp
    commands/command_bake_layer.cpp
)

get_target_property(CLI11_INCLUDE_DIRS CLI11::CLI11 INTERFACE_INCLUDE_DIRECTORIES)

target_link_libraries(chronon3d_cli
    PRIVATE
        chronon3d_pipeline
        chronon3d_backend_image
        spdlog::spdlog_header_only
        fmt::fmt
)

target_include_directories(chronon3d_cli PRIVATE ${CMAKE_SOURCE_DIR})

if(CLI11_INCLUDE_DIRS)
    target_include_directories(chronon3d_cli PRIVATE ${CLI11_INCLUDE_DIRS})
endif()

if(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
    target_link_libraries(chronon3d_cli PRIVATE unofficial::sqlite3::sqlite3)
endif()
