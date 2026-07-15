# ==============================================================================
# Chronon3D CLI — independently gated command groups.
# ==============================================================================

option(CHRONON3D_BUILD_CLI_CORE      "Build the core CLI commands" ON)
option(CHRONON3D_BUILD_CLI_RENDER    "Build the canonical render command" ON)
option(CHRONON3D_BUILD_CLI_VIDEO     "Build the deprecated video command alias" ON)
option(CHRONON3D_BUILD_CLI_TELEMETRY "Build telemetry commands" ON)
option(CHRONON3D_BUILD_CLI_DEV       "Build developer commands" OFF)
option(CHRONON3D_BUILD_CLI_BENCH     "Build benchmark commands" OFF)

set(CHRONON3D_TEMPLATES_DIR "${CMAKE_SOURCE_DIR}/templates")
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/templates_dir.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/templates_dir.hpp
    @ONLY
)

# ------------------------------------------------------------------------------
# Core: list/info/doctor/verify + create/watch/preview/daemon.
# ------------------------------------------------------------------------------
add_library(chronon3d_cli_core STATIC
    commands/group_core.cpp
    commands/create/register_create_commands.cpp
    commands/watch/register_watch_commands.cpp
    commands/preview/register_preview_commands.cpp
    commands/basic/command_list.cpp
    commands/basic/command_info.cpp
    commands/daemon/command_daemon.cpp
    daemon/daemon_service.cpp
    utils/common/cli_utils.cpp
    commands/dev/command_doctor_verify.cpp
)
target_include_directories(chronon3d_cli_core PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli
)
target_link_libraries(chronon3d_cli_core PRIVATE
    chronon3d_pipeline
    CLI11::CLI11
    spdlog::spdlog_header_only
    fmt::fmt
)
target_precompile_headers(chronon3d_cli_core PRIVATE
    <CLI/CLI.hpp>
    <spdlog/spdlog.h>
)

# ------------------------------------------------------------------------------
# Canonical render command and the sole RenderJob executor.
# ------------------------------------------------------------------------------
if(CHRONON3D_BUILD_CLI_RENDER)
    add_library(chronon3d_cli_render STATIC
        commands/group_render.cpp
        commands/render/register_render_commands.cpp
        commands/render/command_render.cpp
        commands/render/command_preflight.cpp
        commands/render/command_still.cpp
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
endif()

# ------------------------------------------------------------------------------
# Low-level video exporters. This is not a second job executor or CLI path.
# The canonical render target owns validation/dry-run/dispatch and links this
# implementation target when media support is enabled.
# ------------------------------------------------------------------------------
if(CHRONON3D_ENABLE_VIDEO)
    add_library(chronon3d_cli_video_export STATIC
        commands/video/common/video_export_common.cpp
        commands/video/common/pipe_export_helpers.cpp
        commands/video/exporters/video_export_pipe.cpp
        commands/video/exporters/pipe_export_pipeline.cpp
        commands/video/exporters/pipe_export_finalize.cpp
        commands/video/common/pipe_export_session.cpp
        commands/video/exporters/video_export_chunked.cpp
        utils/video/video_sink_encoders.cpp
        utils/video/video_sink_adapter.cpp
    )
    target_include_directories(chronon3d_cli_video_export PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli
    )
    target_link_libraries(chronon3d_cli_video_export PRIVATE
        chronon3d_cli_core
        chronon3d_pipeline
        chronon3d_backend_image
        chronon3d_media_video
        CLI11::CLI11
        spdlog::spdlog_header_only
        fmt::fmt
    )

    if(CHRONON3D_ENABLE_NATIVE_FFMPEG)
        target_sources(chronon3d_cli_video_export PRIVATE
            utils/video/native_av_encoder.cpp
            utils/video/native_av_encoder_write.cpp
        )
        target_link_libraries(chronon3d_cli_video_export PRIVATE
            chronon3d_ffmpeg_full
        )
        target_compile_definitions(chronon3d_cli_video_export PRIVATE
            CHRONON3D_ENABLE_NATIVE_FFMPEG
        )
    endif()

    if(TARGET chronon3d_cli_render)
        target_sources(chronon3d_cli_render PRIVATE
            utils/video/video_job_validate.cpp
            utils/video/video_job_dry_run.cpp
            utils/video/video_job_execute.cpp
        )
        target_link_libraries(chronon3d_cli_render PRIVATE
            chronon3d_cli_video_export
        )
        target_compile_definitions(chronon3d_cli_render PRIVATE
            CHRONON3D_HAS_CLI_VIDEO_EXPORT
        )
    endif()
endif()

# ------------------------------------------------------------------------------
# Deprecated `video` alias. It only translates VideoArgs into RenderJob and
# delegates to the canonical render executor. Video export itself remains
# available to `render -o *.mp4` even when this alias target is disabled.
# ------------------------------------------------------------------------------
if(CHRONON3D_ENABLE_VIDEO AND CHRONON3D_BUILD_CLI_VIDEO)
    if(NOT TARGET chronon3d_cli_render)
        message(FATAL_ERROR
            "CHRONON3D_BUILD_CLI_VIDEO requires CHRONON3D_BUILD_CLI_RENDER: "
            "the deprecated video command is an adapter to canonical render")
    endif()

    add_library(chronon3d_cli_video STATIC
        commands/group_video.cpp
        commands/video/register_video_commands.cpp
        commands/video/command_video.cpp
        commands/video/command_video_camera.cpp
        utils/video/video_job_plan.cpp
    )
    target_include_directories(chronon3d_cli_video PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli
    )
    target_link_libraries(chronon3d_cli_video PRIVATE
        chronon3d_cli_core
        chronon3d_cli_render
        chronon3d_cli_video_export
        chronon3d_pipeline
        chronon3d_backend_image
        chronon3d_media_video
        CLI11::CLI11
        spdlog::spdlog_header_only
        fmt::fmt
    )
endif()

# Core diagnostics compile only the legs whose command groups exist.
if(TARGET chronon3d_cli_video)
    target_compile_definitions(chronon3d_cli_core PRIVATE
        CHRONON3D_HAS_CLI_VIDEO
    )
endif()
if(TARGET chronon3d_cli_render)
    target_compile_definitions(chronon3d_cli_core PRIVATE
        CHRONON3D_HAS_CLI_RENDER
    )
endif()

# ------------------------------------------------------------------------------
# Telemetry.
# ------------------------------------------------------------------------------
if(CHRONON3D_ENABLE_TELEMETRY AND CHRONON3D_BUILD_CLI_TELEMETRY)
    add_library(chronon3d_cli_telemetry STATIC
        commands/group_telemetry.cpp
        commands/telemetry/register_telemetry_commands.cpp
        commands/telemetry/command_telemetry.cpp
        commands/telemetry/command_telemetry_helpers.cpp
        commands/telemetry/command_telemetry_summary.cpp
    )
    target_include_directories(chronon3d_cli_telemetry PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli
    )
    target_link_libraries(chronon3d_cli_telemetry PRIVATE
        chronon3d_cli_core
        chronon3d_pipeline
        CLI11::CLI11
        spdlog::spdlog_header_only
        fmt::fmt
        unofficial::sqlite3::sqlite3
    )
    target_compile_definitions(chronon3d_cli_telemetry PUBLIC
        CHRONON3D_ENABLE_SQLITE_TELEMETRY
    )
endif()

# ------------------------------------------------------------------------------
# Developer commands and DEV-only visual compositions.
# ------------------------------------------------------------------------------
if(CHRONON3D_BUILD_CLI_DEV)
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
    if(TARGET chronon3d_cli_video)
        target_link_libraries(chronon3d_cli_dev PRIVATE chronon3d_cli_video)
        target_compile_definitions(chronon3d_cli_dev PRIVATE
            CHRONON3D_HAS_CLI_VIDEO
        )
    endif()
endif()

# ------------------------------------------------------------------------------
# Bench commands.
# ------------------------------------------------------------------------------
if(CHRONON3D_BUILD_CLI_BENCH)
    add_library(chronon3d_cli_bench STATIC
        commands/group_bench.cpp
        commands/bench/register_bench_commands.cpp
        commands/bench/command_bench.cpp
    )
    target_include_directories(chronon3d_cli_bench PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli
    )
    target_link_libraries(chronon3d_cli_bench PRIVATE
        chronon3d_cli_core
        chronon3d_pipeline
        CLI11::CLI11
        spdlog::spdlog_header_only
        fmt::fmt
    )
    if(TARGET benchmark::benchmark)
        target_link_libraries(chronon3d_cli_bench PUBLIC benchmark::benchmark)
        target_compile_definitions(chronon3d_cli_bench PRIVATE
            CHRONON3D_BUILD_BENCHMARKS
        )
    endif()
endif()

# ------------------------------------------------------------------------------
# Final CLI executable.
# ------------------------------------------------------------------------------
add_executable(chronon3d_cli
    main.cpp
    register_runtime_compositions.cpp
    register_content_compositions.cpp
    register_dev_compositions.cpp
    $<$<AND:$<BOOL:${CHRONON3D_ENABLE_TEXT}>,$<BOOL:${CHRONON3D_USE_BLEND2D}>>:${CMAKE_SOURCE_DIR}/examples/bench_corpus/bench_corpus_scenes.cpp>
)

get_target_property(CLI11_INCLUDE_DIRS CLI11::CLI11
                    INTERFACE_INCLUDE_DIRECTORIES)

target_link_libraries(chronon3d_cli PRIVATE
    chronon3d_sdk
    chronon3d_cli_core
    chronon3d_backend_image
    spdlog::spdlog_header_only
    fmt::fmt
)
foreach(_cli_target IN ITEMS
        chronon3d_cli_render
        chronon3d_cli_video_export
        chronon3d_cli_video
        chronon3d_cli_dev
        chronon3d_cli_bench
        chronon3d_cli_telemetry)
    if(TARGET ${_cli_target})
        target_link_libraries(chronon3d_cli PRIVATE ${_cli_target})
    endif()
endforeach()

if(TARGET chronon3d_backend_text)
    target_link_libraries(chronon3d_cli PRIVATE chronon3d_backend_text)
endif()
if(CLI11_INCLUDE_DIRS)
    target_include_directories(chronon3d_cli PRIVATE ${CLI11_INCLUDE_DIRS})
endif()
if(CHRONON3D_BUILD_CONTENT OR CHRONON3D_BUILD_DIAGNOSTICS)
    if(TARGET chronon3d_content)
        target_link_libraries(chronon3d_cli PRIVATE chronon3d_content)
    endif()
    target_compile_definitions(chronon3d_cli PRIVATE CHRONON3D_BUILD_CONTENT)
endif()

target_include_directories(chronon3d_cli PRIVATE ${CMAKE_SOURCE_DIR})
target_precompile_headers(chronon3d_cli PRIVATE
    <algorithm>
    <atomic>
    <condition_variable>
    <deque>
    <filesystem>
    <memory>
    <mutex>
    <thread>
    <vector>
    <spdlog/spdlog.h>
    <CLI/CLI.hpp>
)

if(CHRONON3D_ENABLE_TELEMETRY)
    target_link_libraries(chronon3d_cli PRIVATE unofficial::sqlite3::sqlite3)
endif()

set_target_properties(chronon3d_cli_core chronon3d_cli PROPERTIES
    UNITY_BUILD OFF
)
foreach(_cli_target IN ITEMS
        chronon3d_cli_render
        chronon3d_cli_video_export
        chronon3d_cli_video
        chronon3d_cli_dev
        chronon3d_cli_bench
        chronon3d_cli_telemetry)
    if(TARGET ${_cli_target})
        set_target_properties(${_cli_target} PROPERTIES UNITY_BUILD OFF)
    endif()
endforeach()
