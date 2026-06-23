# ==============================================================================
# CLI Command Groups — 6 independent static libs
# Each group can be compiled/linked independently based on build profile.
# ==============================================================================

# ==============================================================================
# PR-6: CLI Build Options (sub-target gating)
# --------------------------------------------------------------------------
# Every CLI sub-target is gated behind a CHRONON3D_BUILD_CLI_<GROUP> option
# (default ON). Each sub-target wraps its add_library() block in
# `if(CHRONON3D_BUILD_CLI_<GROUP>) ... endif()`; the executable
# `chronon3d_cli` links each gated sub-target only when its target exists at
# configure time (canonical idiom: `if(TARGET <sub>)` guards, NOT option-name
# string references — keeps the link closure self-documenting).
# --------------------------------------------------------------------------
option(CHRONON3D_BUILD_CLI_CORE      "Build the chronon3d_cli_core sub-target (always ON; list/info/doctor/verify + daemon/watch sources)" ON)
option(CHRONON3D_BUILD_CLI_RENDER    "Build the chronon3d_cli_render sub-target (render/preflight/proofs/bake_layer/graph commands)" ON)
option(CHRONON3D_BUILD_CLI_VIDEO     "Build the chronon3d_cli_video sub-target (video/ffmpeg-pipe commands; also requires CHRONON3D_ENABLE_VIDEO)" ON)
option(CHRONON3D_BUILD_CLI_TELEMETRY "Build the chronon3d_cli_telemetry sub-target (telemetry summary/report; also requires CHRONON3D_ENABLE_TELEMETRY)" ON)
option(CHRONON3D_BUILD_CLI_DEV       "Build the chronon3d_cli_dev sub-target (batch/studio/inspect/camera_path/dev-tools)" OFF)
option(CHRONON3D_BUILD_CLI_BENCH     "Build the chronon3d_cli_bench sub-target (benchmark commands; also requires CHRONON3D_BUILD_BENCHMARKS)" OFF)
# --------------------------------------------------------------------------
# Forward-compat placeholders: daemon + watch commands currently live inside
# chronon3d_cli_core (sources: commands/daemon/command_daemon.cpp,
# daemon/daemon_service.cpp + commands/basic/command_watch.cpp,
# utils/watch/watch_service.cpp). The flags below document the future
# extraction intent without modifying any compilation today; flipping them
# OFF once the dedicated sub-targets exist will prune daemon/watch code from
# _core as a single-bot follow-up PR.
# --------------------------------------------------------------------------

# Group: Core (list, info, doctor, verify) — always included
add_library(chronon3d_cli_core STATIC
    commands/group_core.cpp
    commands/basic/command_list.cpp
    commands/basic/command_info.cpp
    commands/basic/command_watch.cpp
    commands/daemon/command_daemon.cpp
    daemon/daemon_service.cpp
    utils/common/cli_utils.cpp
    utils/watch/watch_service.cpp
)
target_include_directories(chronon3d_cli_core PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli)
target_link_libraries(chronon3d_cli_core PRIVATE chronon3d_pipeline CLI11::CLI11 spdlog::spdlog_header_only fmt::fmt)
target_precompile_headers(chronon3d_cli_core PRIVATE <CLI/CLI.hpp> <spdlog/spdlog.h>)

# Group: Render (render, preflight, proofs, bake_layer, graph)
if(CHRONON3D_BUILD_CLI_RENDER)
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
target_include_directories(chronon3d_cli_render PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli)
target_link_libraries(chronon3d_cli_render PRIVATE chronon3d_cli_core chronon3d_pipeline chronon3d_backend_image CLI11::CLI11 spdlog::spdlog_header_only fmt::fmt)
endif()  # CHRONON3D_BUILD_CLI_RENDER

# Group: Video (video, ffmpeg_pipe)
if(CHRONON3D_ENABLE_VIDEO AND CHRONON3D_BUILD_CLI_VIDEO)
add_library(chronon3d_cli_video STATIC
    commands/group_video.cpp
    commands/video/register_video_commands.cpp
    commands/video/command_video.cpp
    commands/video/command_video_camera.cpp
    commands/video/common/video_export_common.cpp
    commands/video/common/pipe_export_helpers.cpp
    commands/video/exporters/video_export_pipe.cpp
    commands/video/exporters/pipe_export_session_setup.cpp
    commands/video/exporters/pipe_export_pool_warmup.cpp
    commands/video/exporters/pipe_export_loop.cpp
    commands/video/exporters/pipe_export_encoder_close.cpp
    commands/video/exporters/pipe_export_telemetry.cpp
    commands/video/exporters/pipe_export_result.cpp
    commands/video/common/pipe_export_session.cpp
    commands/video/exporters/video_export_chunked.cpp

    utils/video/video_job_plan.cpp
    utils/video/video_job_validate.cpp
    utils/video/video_job_dry_run.cpp
    utils/video/video_job_execute.cpp
    utils/video/video_sink_encoders.cpp
    utils/video/video_sink_adapter.cpp
)
target_include_directories(chronon3d_cli_video PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli)
target_link_libraries(chronon3d_cli_video PRIVATE chronon3d_cli_core chronon3d_pipeline chronon3d_backend_image chronon3d_media_video CLI11::CLI11 spdlog::spdlog_header_only fmt::fmt)

if(CHRONON3D_ENABLE_NATIVE_FFMPEG)
    target_sources(chronon3d_cli_video PRIVATE
        utils/video/native_av_encoder.cpp
        utils/video/native_av_encoder_write.cpp
    )
    target_link_libraries(chronon3d_cli_video PRIVATE chronon3d_ffmpeg_full)
    target_compile_definitions(chronon3d_cli_video PRIVATE CHRONON3D_ENABLE_NATIVE_FFMPEG)
endif()
endif() # CHRONON3D_ENABLE_VIDEO

# Group: Telemetry (telemetry summary/report)
if(CHRONON3D_ENABLE_TELEMETRY AND CHRONON3D_BUILD_CLI_TELEMETRY)
add_library(chronon3d_cli_telemetry STATIC
    commands/group_telemetry.cpp
    commands/telemetry/register_telemetry_commands.cpp
    commands/telemetry/command_telemetry.cpp
    commands/telemetry/command_telemetry_helpers.cpp
    commands/telemetry/command_telemetry_summary.cpp
)
target_include_directories(chronon3d_cli_telemetry PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli)
target_link_libraries(chronon3d_cli_telemetry PRIVATE chronon3d_cli_core chronon3d_pipeline CLI11::CLI11 spdlog::spdlog_header_only fmt::fmt unofficial::sqlite3::sqlite3)
target_compile_definitions(chronon3d_cli_telemetry PUBLIC CHRONON3D_ENABLE_SQLITE_TELEMETRY)
endif()

# Group: Dev (batch, studio, inspect)
if(CHRONON3D_BUILD_CLI_DEV)
add_library(chronon3d_cli_dev STATIC
    commands/group_dev.cpp
    commands/dev/register_dev_commands.cpp
    commands/dev/register_inspect_commands.cpp
    commands/dev/command_batch.cpp
    commands/dev/command_bench_convert.cpp
    commands/dev/command_doctor_verify.cpp
    commands/dev/command_camera_path.cpp
    $<$<AND:$<BOOL:${CHRONON3D_USE_BLEND2D}>,$<BOOL:${CHRONON3D_ENABLE_TEXT}>>:commands/dev/command_text_audit.cpp>
    $<$<AND:$<BOOL:${CHRONON3D_USE_BLEND2D}>,$<BOOL:${CHRONON3D_ENABLE_TEXT}>>:commands/dev/text_audit_engine.cpp>
    $<$<AND:$<BOOL:${CHRONON3D_USE_BLEND2D}>,$<BOOL:${CHRONON3D_ENABLE_TEXT}>>:commands/dev/text_audit_json.cpp>
    utils/batch/batch_job_spec.cpp
    utils/batch/batch_runner.cpp
)
target_include_directories(chronon3d_cli_dev PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli)
target_link_libraries(chronon3d_cli_dev PRIVATE chronon3d_cli_core chronon3d_cli_render chronon3d_pipeline CLI11::CLI11 spdlog::spdlog_header_only fmt::fmt)
if(TARGET chronon3d_cli_video)
    target_link_libraries(chronon3d_cli_dev PRIVATE chronon3d_cli_video)
    target_compile_definitions(chronon3d_cli_dev PRIVATE CHRONON3D_HAS_CLI_VIDEO)
endif()
endif()  # CHRONON3D_BUILD_CLI_DEV

# Group: Bench (benchmark commands)
if(CHRONON3D_BUILD_CLI_BENCH)
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
endif()  # CHRONON3D_BUILD_CLI_BENCH

# ==============================================================================
# CLI Executable — links all groups
# ==============================================================================
add_executable(chronon3d_cli
    main.cpp
)

get_target_property(CLI11_INCLUDE_DIRS CLI11::CLI11 INTERFACE_INCLUDE_DIRECTORIES)

# PR-6: Chronon3D::SDK (via chronon3d_sdk INTERFACE = Chronon3D::SDK alias) is the
# canonical SDK surface and is unconditionally linked.  The transitively-imported
# OBJECT libs from chronon3d_pipeline resolve every symbol for in-tree consumers,
# so an explicit link to chronon3d_sdk_impl is redundant and bypasses the
# boundary that Chronon3D::SDK exists to certify.  chronon3d_cli_core is likewise
# unconditional.  The gated sub-targets (_render, _dev, _bench) are linked ONLY
# when their target exists at configure time — canonical idiom for option()-
# driven compilation gating.
target_link_libraries(chronon3d_cli PRIVATE
    chronon3d_sdk
    chronon3d_cli_core
    chronon3d_backend_image
    spdlog::spdlog_header_only
    fmt::fmt
)
foreach(_cli_target IN ITEMS chronon3d_cli_render chronon3d_cli_dev chronon3d_cli_bench chronon3d_cli_video chronon3d_cli_telemetry)
    if(TARGET ${_cli_target})
        target_link_libraries(chronon3d_cli PRIVATE ${_cli_target})
    endif()
endforeach()

# chronon3d_backend_text is gated by CHRONON3D_ENABLE_TEXT && CHRONON3D_USE_BLEND2D
# upstream — only link it when the target exists (otherwise the CLI breaks
# builds configured without text/blend2d).
if(TARGET chronon3d_backend_text)
    target_link_libraries(chronon3d_cli PRIVATE chronon3d_backend_text)
endif()

if(CLI11_INCLUDE_DIRS)
    target_include_directories(chronon3d_cli PRIVATE ${CLI11_INCLUDE_DIRS})
endif()

# Optional content linking (proofs/demos) — single registry target
if(CHRONON3D_BUILD_CONTENT OR CHRONON3D_BUILD_DIAGNOSTICS)
    if(TARGET chronon3d_content)
        target_link_libraries(chronon3d_cli PRIVATE chronon3d_content)
    endif()
    target_compile_definitions(chronon3d_cli PRIVATE CHRONON3D_BUILD_CONTENT)
endif()

target_include_directories(chronon3d_cli PRIVATE ${CMAKE_SOURCE_DIR})

# FIX: GCC 14 + Unity-build — <chrono> and <fmt/format.h> cascade
# through unity-aggregated TUs (transitively-visible headers like
# <ratio>/<version>/... lost across aggregator boundaries, 'time_point'
# and friends fail to resolve inside /usr/include/c++/14/chrono, and
# fmt format-string formatting surface breaks similarly).  Force
# per-TU include by excluding them from the CLI PCH list.  Per-TU
# include cost is negligible (compiler caches standard headers).
target_precompile_headers(chronon3d_cli PRIVATE
    <algorithm> <atomic> <condition_variable> <deque>
    <filesystem> <memory> <mutex> <thread> <vector>
    <spdlog/spdlog.h> <CLI/CLI.hpp>
)

if(CHRONON3D_ENABLE_TELEMETRY)
    target_link_libraries(chronon3d_cli PRIVATE unofficial::sqlite3::sqlite3)
endif()

# FIX: GCC 14 + Unity-build + third-party transitive include cascade.
# <chrono> and <fmt/format.h> are pulled in transitively via <CLI/CLI.hpp>
# and <spdlog/spdlog.h>; aggregated into unity TUs the include order loses
# the standard-library transitive deps that /usr/include/c++/14/chrono and
# /fmt/format.h require (time_point, seconds, days, local_time, utc_time,
# is_printable, ...).  Compile each CLI TU independently so its include
# order is preserved.  Unity builds remain ON for the heavy engine
# libraries (chronon3d_pipeline, chronon3d_backend_*, chronon3d_graph_*)
# where they actually buy meaningful compile-time.
# PR-6: each gated sub-target is wrapped in `if(TARGET ...)` so disabled groups
# are gracefully skipped, preserving the GCC 14 unity-build fix isolation.
set_target_properties(chronon3d_cli_core chronon3d_cli PROPERTIES UNITY_BUILD OFF)
foreach(_cli_target IN ITEMS chronon3d_cli_render chronon3d_cli_dev chronon3d_cli_bench chronon3d_cli_video chronon3d_cli_telemetry)
    if(TARGET ${_cli_target})
        set_target_properties(${_cli_target} PROPERTIES UNITY_BUILD OFF)
    endif()
endforeach()
