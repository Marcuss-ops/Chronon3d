# ==============================================================================
# CLI Command Groups — 6 independent static libs
# Each group can be compiled/linked independently based on build profile.
# ==============================================================================

# Group: Core (list, info, doctor, verify) — always included
add_library(chronon3d_cli_core STATIC
    commands/group_core.cpp
    commands/basic/command_list.cpp
    commands/basic/command_info.cpp
    commands/basic/command_watch.cpp
    utils/common/cli_utils.cpp
    utils/watch/watch_service.cpp
)
target_include_directories(chronon3d_cli_core PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli)
target_link_libraries(chronon3d_cli_core PRIVATE chronon3d_pipeline CLI11::CLI11 spdlog::spdlog_header_only fmt::fmt)
target_precompile_headers(chronon3d_cli_core PRIVATE <CLI/CLI.hpp> <spdlog/spdlog.h>)

# Group: Render (render, preflight, proofs, bake_layer, graph)
add_library(chronon3d_cli_render STATIC
    commands/group_render.cpp
    commands/render/register_render_commands.cpp
    commands/render/command_render.cpp
    commands/render/command_preflight.cpp
    commands/render/command_proofs.cpp
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
target_include_directories(chronon3d_cli_render PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli)
target_link_libraries(chronon3d_cli_render PRIVATE chronon3d_cli_core chronon3d_pipeline chronon3d_backend_image CLI11::CLI11 spdlog::spdlog_header_only fmt::fmt)

# Group: Video (video, ffmpeg_pipe)
add_library(chronon3d_cli_video STATIC
    commands/group_video.cpp
    commands/video/register_video_commands.cpp
    commands/video/command_video.cpp
    commands/video/command_video_camera.cpp
    commands/video/common/video_export_common.cpp
    commands/video/common/pipe_export_helpers.cpp
    commands/video/common/pipe_export_setup.cpp
    commands/video/common/pipe_export_close.cpp
    commands/video/common/pipe_export_telemetry.cpp
    commands/video/exporters/video_export_pipe.cpp
    commands/video/exporters/pipe_export_session_setup.cpp
    commands/video/exporters/pipe_export_pool_warmup.cpp
    commands/video/exporters/pipe_export_loop.cpp
    commands/video/exporters/pipe_export_encoder_close.cpp
    commands/video/exporters/pipe_export_telemetry.cpp
    commands/video/exporters/pipe_export_result.cpp
    commands/video/common/pipe_export_session.cpp
    commands/video/exporters/video_export_chunked.cpp
    commands/video/exporter_registry.cpp
    commands/video/exporters/pipe_exporter.cpp
    commands/video/exporters/chunked_exporter.cpp
    utils/video/ffmpeg_pipe_encoder.cpp
    utils/video/ffmpeg_pipe_write_frame.cpp
    utils/video/ffmpeg_pipe_encoder_uring.cpp
    utils/video/ffmpeg_pipe_command.cpp
    utils/video/ffmpeg_pipe_yuv.cpp
    utils/video/video_job_plan.cpp
    utils/video/video_job_validate.cpp
    utils/video/video_job_dry_run.cpp
    utils/video/video_job_execute.cpp
)
target_include_directories(chronon3d_cli_video PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli)
target_link_libraries(chronon3d_cli_video PRIVATE chronon3d_cli_core chronon3d_pipeline chronon3d_backend_image CLI11::CLI11 spdlog::spdlog_header_only fmt::fmt)

if(CHRONON3D_ENABLE_NATIVE_FFMPEG)
    target_sources(chronon3d_cli_video PRIVATE
        utils/video/native_av_encoder.cpp
        utils/video/native_av_encoder_write.cpp
    )
    # Prefer the modern IMPORTED target; fall back to the legacy string list
    # if the imported target wasn't created (e.g. when src/CMakeLists.txt
    # hasn't been loaded yet for a partial configure).
    if(TARGET chronon3d_ffmpeg_full)
        target_link_libraries(chronon3d_cli_video PRIVATE chronon3d_ffmpeg_full)
    else()
        target_link_libraries(chronon3d_cli_video PRIVATE ${CHRONON3D_FFMPEG_LIBS_FULL})
    endif()
    target_compile_definitions(chronon3d_cli_video PRIVATE CHRONON3D_ENABLE_NATIVE_FFMPEG)
endif()

# Group: Telemetry (telemetry summary/report)
add_library(chronon3d_cli_telemetry STATIC
    commands/group_telemetry.cpp
    commands/telemetry/register_telemetry_commands.cpp
    commands/telemetry/command_telemetry.cpp
    commands/telemetry/command_telemetry_helpers.cpp
    commands/telemetry/command_telemetry_summary.cpp
    commands/telemetry/command_telemetry_report.cpp
    commands/telemetry/telemetry_report_model.cpp
    commands/telemetry/telemetry_report_analyzer.cpp
    commands/telemetry/telemetry_report_markdown.cpp
    commands/telemetry/telemetry_report_sections/overview_perf.cpp
    commands/telemetry/telemetry_report_sections/frame_summary.cpp
    commands/telemetry/telemetry_report_sections/core_render_phases.cpp
    commands/telemetry/telemetry_report_sections/phase_cpu_efficiency.cpp
    commands/telemetry/telemetry_report_sections/counters_metrics.cpp
    commands/telemetry/telemetry_report_sections/framebuffer_pool.cpp
    commands/telemetry/telemetry_report_sections/hot_nodes.cpp
    commands/telemetry/telemetry_report_sections/hot_work_attribution.cpp
    commands/telemetry/telemetry_report_sections/layer_cost_breakdown.cpp
    commands/telemetry/telemetry_report_sections/frame_samples.cpp
    commands/telemetry/telemetry_report_sections/cache_diagnostics.cpp
    commands/telemetry/telemetry_report_sections/parallelism_decisions.cpp
    commands/telemetry/telemetry_report_sections/system_resources.cpp
    commands/telemetry/telemetry_report_sections/bottleneck_diagnosis.cpp
)
target_include_directories(chronon3d_cli_telemetry PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli)
target_link_libraries(chronon3d_cli_telemetry PRIVATE chronon3d_cli_core chronon3d_pipeline CLI11::CLI11 spdlog::spdlog_header_only fmt::fmt)

if(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
    target_link_libraries(chronon3d_cli_telemetry PRIVATE unofficial::sqlite3::sqlite3)
endif()

# Group: Dev (batch, studio, inspect)
add_library(chronon3d_cli_dev STATIC
    commands/group_dev.cpp
    commands/dev/register_dev_commands.cpp
    commands/dev/register_inspect_commands.cpp
    commands/dev/command_studio.cpp
    commands/dev/command_batch.cpp
    commands/dev/command_bench_convert.cpp
    commands/dev/command_doctor_verify.cpp
    commands/dev/command_camera_path.cpp
    utils/batch/batch_job_spec.cpp
    utils/batch/batch_runner.cpp
)
target_include_directories(chronon3d_cli_dev PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli)
target_link_libraries(chronon3d_cli_dev PRIVATE chronon3d_cli_core chronon3d_cli_render chronon3d_pipeline CLI11::CLI11 spdlog::spdlog_header_only fmt::fmt)

# Group: Bench (benchmark commands)
add_library(chronon3d_cli_bench STATIC
    commands/group_bench.cpp
    commands/bench/register_bench_commands.cpp
    commands/bench/command_bench.cpp
)
target_include_directories(chronon3d_cli_bench PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli)
target_link_libraries(chronon3d_cli_bench PRIVATE chronon3d_cli_core chronon3d_pipeline CLI11::CLI11 spdlog::spdlog_header_only fmt::fmt)

if(TARGET benchmark::benchmark)
    target_link_libraries(chronon3d_cli_bench PUBLIC benchmark::benchmark)
    target_compile_definitions(chronon3d_cli_bench PRIVATE CHRONON3D_BUILD_BENCHMARKS)
endif()

# ==============================================================================
# CLI Executable — links all groups
# ==============================================================================
add_executable(chronon3d_cli
    main.cpp
)

get_target_property(CLI11_INCLUDE_DIRS CLI11::CLI11 INTERFACE_INCLUDE_DIRECTORIES)

target_link_libraries(chronon3d_cli PRIVATE
    chronon3d_cli_core
    chronon3d_cli_render
    chronon3d_cli_telemetry
    chronon3d_cli_video
    chronon3d_cli_dev
    chronon3d_cli_bench
    chronon3d_backend_image
    spdlog::spdlog_header_only
    fmt::fmt
)

if(CLI11_INCLUDE_DIRS)
    target_include_directories(chronon3d_cli PRIVATE ${CLI11_INCLUDE_DIRS})
endif()

# Optional content linking (proofs/demos) — single registry target
if(CHRONON3D_BUILD_CONTENT)
    if(TARGET chronon3d_content_registry)
        target_link_libraries(chronon3d_cli PRIVATE chronon3d_content_registry)
    endif()
    target_compile_definitions(chronon3d_cli PRIVATE CHRONON3D_BUILD_CONTENT)
endif()

target_include_directories(chronon3d_cli PRIVATE ${CMAKE_SOURCE_DIR})

target_precompile_headers(chronon3d_cli PRIVATE
    <algorithm> <atomic> <condition_variable> <chrono> <deque>
    <filesystem> <memory> <mutex> <thread> <vector>
    <spdlog/spdlog.h> <fmt/format.h> <CLI/CLI.hpp>
)

# SQLite for telemetry group
if(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
    target_link_libraries(chronon3d_cli PRIVATE unofficial::sqlite3::sqlite3)
endif()
