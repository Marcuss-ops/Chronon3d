add_executable(chronon3d_cli
    main.cpp
    command_registry.cpp
    commands/basic/register_basic_commands.cpp
    commands/render/register_render_commands.cpp
    commands/video/register_video_commands.cpp
    commands/dev/register_dev_commands.cpp
    commands/dev/register_inspect_commands.cpp
    commands/bench/register_bench_commands.cpp
    commands/telemetry/register_telemetry_commands.cpp
    utils/common/cli_utils.cpp
    utils/job/cli_render_utils.cpp
    utils/video/ffmpeg_pipe_encoder.cpp
    utils/video/ffmpeg_pipe_write_frame.cpp
    utils/video/ffmpeg_pipe_encoder_uring.cpp
    utils/video/ffmpeg_pipe_command.cpp
    utils/video/ffmpeg_pipe_yuv.cpp
    ${CMAKE_SOURCE_DIR}/src/video/frame_converter.cpp
    ${CMAKE_SOURCE_DIR}/src/video/converted_frame_cache.cpp
    utils/job/render_job.cpp
    utils/job/render_job_write_frame.cpp
    utils/job/render_job_execute.cpp
    utils/batch/batch_job_spec.cpp
    utils/batch/batch_runner.cpp
    utils/watch/watch_service.cpp

    commands/basic/command_list.cpp
    commands/basic/command_info.cpp
    commands/render/command_render.cpp
    commands/dev/command_studio.cpp
    commands/dev/command_batch.cpp
    commands/basic/command_watch.cpp
    commands/video/command_video.cpp
    commands/video/video_export_common.cpp
    commands/video/video_export_pipe.cpp
    commands/video/video_export_chunked.cpp
    commands/dev/command_doctor_verify.cpp
    commands/bench/command_bench.cpp
    commands/render/command_graph.cpp
    commands/render/command_preflight.cpp
    commands/render/command_proofs.cpp
    commands/telemetry/command_telemetry.cpp
    commands/telemetry/command_telemetry_helpers.cpp
    commands/telemetry/command_telemetry_summary.cpp
    commands/telemetry/command_telemetry_report.cpp
    commands/render/command_bake_layer.cpp

)
set_target_properties(chronon3d_cli PROPERTIES UNITY_BUILD OFF)

get_target_property(CLI11_INCLUDE_DIRS CLI11::CLI11 INTERFACE_INCLUDE_DIRECTORIES)

target_link_libraries(chronon3d_cli
    PRIVATE
        chronon3d_pipeline
        chronon3d_backend_image
        $<LINK_LIBRARY:WHOLE_ARCHIVE,chronon3d_content>
        CLI11::CLI11
        spdlog::spdlog_header_only
        fmt::fmt
        benchmark::benchmark
)

target_include_directories(chronon3d_cli PRIVATE ${CMAKE_SOURCE_DIR})

if(CLI11_INCLUDE_DIRS)
    target_include_directories(chronon3d_cli PRIVATE ${CLI11_INCLUDE_DIRS})
endif()

target_precompile_headers(chronon3d_cli PRIVATE
    <algorithm>
    <atomic>
    <condition_variable>
    <chrono>
    <deque>
    <filesystem>
    <memory>
    <mutex>
    <thread>
    <vector>
    <spdlog/spdlog.h>
    <fmt/format.h>
    <CLI/CLI.hpp>
)

if(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
    target_link_libraries(chronon3d_cli PRIVATE unofficial::sqlite3::sqlite3)
endif()
