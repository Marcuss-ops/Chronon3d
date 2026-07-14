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
option(CHRONON3D_BUILD_CLI_CORE      "Build the chronon3d_cli_core sub-target (always ON; list/info/doctor/verify + daemon sources)" ON)
option(CHRONON3D_BUILD_CLI_RENDER    "Build the chronon3d_cli_render sub-target (render/preflight/proofs/bake_layer/graph commands)" ON)
option(CHRONON3D_BUILD_CLI_VIDEO     "Build the chronon3d_cli_video sub-target (video/ffmpeg-pipe commands; also requires CHRONON3D_ENABLE_VIDEO)" ON)
option(CHRONON3D_BUILD_CLI_TELEMETRY "Build the chronon3d_cli_telemetry sub-target (telemetry summary/report; also requires CHRONON3D_ENABLE_TELEMETRY)" ON)
option(CHRONON3D_BUILD_CLI_DEV       "Build the chronon3d_cli_dev sub-target (batch/studio/inspect/camera_path/dev-tools)" OFF)
option(CHRONON3D_BUILD_CLI_BENCH     "Build the chronon3d_cli_bench sub-target (benchmark commands; also requires CHRONON3D_BUILD_BENCHMARKS)" OFF)
# --------------------------------------------------------------------------
# Forward-compat placeholders: daemon commands currently live inside
# chronon3d_cli_core (sources: commands/daemon/command_daemon.cpp,
# daemon/daemon_service.cpp). The flag below documents the future
# extraction intent without modifying any compilation today; flipping it
# OFF once the dedicated sub-target exists will prune daemon code from
# _core as a single-bot follow-up PR.
# cron daemon extraction deferred to follow-up PR
# --------------------------------------------------------------------------

# Group: Core (list, info, doctor, verify) — always included.
# command_doctor_verify.cpp must remain in core (not dev): it implements
# doctor + verify, which are core subcommands; moving it to dev breaks the
# "always included" promise when CHRONON3D_BUILD_CLI_DEV=OFF.  The
# #ifdef CHRONON3D_HAS_CLI_VIDEO / CHRONON3D_HAS_CLI_RENDER blocks in
# that file are compile-defined on _core when the respective group is
# built, preserving the live-video and live-render paths without
# unconditionally dragging in the render/video sub-targets.
# TICKET-V3-CLI-UNIFICATION-STARTER-TEMPLATE (Blocco 4.2) -
# Generate templates_dir.hpp with the baked-in absolute path to the
# templates/ directory.  At runtime, CHRONON3D_TEMPLATES_DIR env var
# overrides this baked path.  See templates_dir.hpp.in for the
# @-substitution template.
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/templates_dir.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/templates_dir.hpp
    @ONLY
)

add_library(chronon3d_cli_core STATIC
    commands/group_core.cpp
    # TICKET-V3-CLI-UNIFICATION-STARTER-TEMPLATE (Blocco 4.2) -
    # `chronon create <name>` scaffolds a new project from templates/.
    commands/create/register_create_commands.cpp
    commands/basic/command_list.cpp
    commands/basic/command_info.cpp
    commands/daemon/command_daemon.cpp
    daemon/daemon_service.cpp
    utils/common/cli_utils.cpp
    commands/dev/command_doctor_verify.cpp
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
    commands/video/exporters/pipe_export_pipeline.cpp
    commands/video/exporters/pipe_export_finalize.cpp
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

# Propagate CHRONON3D_HAS_CLI_<GROUP> defines to core AFTER each group has
# been added by add_library(), so the if(TARGET ...) guards see those
# targets.  Earlier placement inside chronon3d_cli_core's library block ran
# BEFORE the gated targets existed — the propagation silently no-op'd and
# builds with the gated group ON + _DEV=OFF lost their guard blocks in
# `verify` (the docstring "core ships doctor + verify including their
# video-camera / render legs" stopped holding).  chronon3d_cli_core is
# always added (ON by default), so no extra TARGET guard is needed here.
if(TARGET chronon3d_cli_video)
    target_compile_definitions(chronon3d_cli_core PRIVATE CHRONON3D_HAS_CLI_VIDEO)
endif()
if(TARGET chronon3d_cli_render)
    target_compile_definitions(chronon3d_cli_core PRIVATE CHRONON3D_HAS_CLI_RENDER)
endif()

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
    commands/dev/command_camera_path.cpp
    commands/dev/command_inspect_text.cpp
    commands/dev/command_text_def_inspect.cpp  # TICKET-CHRONON-GLOW-FINAL Fase 5 — real text_audit_snapshots() consumption
    # P1-7 Chore 1 (commit B) — DELETED 4 legacy text-audit sources wholesale:
    #   commands/dev/command_text_audit.cpp
    #   commands/dev/text_audit_engine.cpp
    #   commands/dev/text_audit_typewriter.cpp
    # plus the audit_result_to_json shim:
    #   commands/dev/text_inspection_json.{hpp,cpp}
    # Reason: these were the only callers of `rasterize_text_to_bl_image()` in
    # active code (Commit A deleted the production caller; CLI + test
    # callers now also retired).  text_audit_helpers.{hpp,cpp} stays —
    # still used by command_inspect_text.cpp for json_escape/json_bbox.
    $<$<AND:$<BOOL:${CHRONON3D_USE_BLEND2D}>,$<BOOL:${CHRONON3D_ENABLE_TEXT}>>:commands/dev/text_audit_helpers.cpp>
    # Step 9 §B — extracted collector (snapshots → TextVisibilityAudit
    # + status mapping). Bridges command_inspect_text.cpp to
    # audit_text_visibility(). DIAGNOSTICS-gated inside.
    $<$<AND:$<BOOL:${CHRONON3D_USE_BLEND2D}>,$<BOOL:${CHRONON3D_ENABLE_TEXT}>>:commands/dev/text_inspection_collector.cpp>
    # P1-7 Chore 1 (commit B) — REMOVED `text_inspection_json.cpp` (the
    # Step-9-§B thin shim whose SOLE purpose was hosting `audit_result_to_json`).
    # With audit_result_to_json deleted wholesale along with the legacy
    # text-audit engine, the .cpp body became empty; deleted it entirely
    # per Cat-3 minimal-surface (no dead code).  Callers that need
    # `json_escape`/`json_bbox` include `text_audit_helpers.hpp` directly
    # (text_inspection_json.{hpp,cpp} re-fetch was an unused façade).
    utils/batch/batch_job_spec.cpp
    utils/batch/batch_runner.cpp
    # TICKET-CLI-ISOLATE-RUNTIME-DEV — tests/visual/*.cpp are compiled into
    # the DEV sub-target (NOT into the production chronon3d_cli executable).
    # This satisfies the user-spec verbatim "Nella produzione il CLI non
    # compila più sorgenti da tests/" + "tests/visual/ resta solo come
    # cartella di golden/fixture — nessuna factory registrata da qui
    # direttamente" mandates.  The DEV register_dev_compositions.cpp (in
    # apps/chronon3d_cli/) is the canonical registration site for the
    # DEV-only compositions (PipelineParityCanary, AE_CAM_*, etc.).
    # Azione 17 — AE_CAM_01..10 factory bodies RELOCATED from
    #   tests/visual/ae_parity/ae_parity_scenes.cpp
    # to
    #   content/ae_parity/ae_cam_scenes.cpp
    # (production-side namespace `chronon3d::content::ae_parity::ae_cam_scenes`,
    #  DEV-only gating preserved via this source list).  tests/visual/ae_parity/
    #  keeps only ae_parity_compositions.cpp (the 5 ae_08_glow_pulse / ae_10_scale_pop
    #  / ae_12 / ae_14 / motion_blur_text floor-dashboard compositions) +
    #  ae_parity_tests.cpp (golden / fixture / asserts, NOT a CLI consumer).
    ${CMAKE_SOURCE_DIR}/content/ae_parity/ae_cam_scenes.cpp
    ${CMAKE_SOURCE_DIR}/tests/visual/camera_truth/camera_truth_test.cpp
    ${CMAKE_SOURCE_DIR}/tests/visual/camera_truth/camera_truth_orbit.cpp
    ${CMAKE_SOURCE_DIR}/tests/visual/ae_parity/ae_parity_compositions.cpp
    ${CMAKE_SOURCE_DIR}/tests/visual/glow_ab/glow_ab_compositions.cpp
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
    # TICKET-CLI-ISOLATE-RUNTIME-DEV — composition registration split into
    # 3 layers (see register_compositions.hpp).  The 3 register_*.cpp files
    # are compiled UNCONDITIONALLY into the CLI (they are tiny thin
    # orchestrators; register_dev_compositions.cpp is empty when
    # CHRONON3D_BUILD_CLI_DEV=OFF, guarded by the canonical #ifdef at
    # the top of the file).
    register_runtime_compositions.cpp
    register_content_compositions.cpp
    register_dev_compositions.cpp
    # TICKET-BENCH-CORPUS-V1 — 12-scene benchmark corpus (B00-B11) + 3
    # B07 inner precomps. GATED under BOTH CHRONON3D_ENABLE_TEXT AND
    # CHRONON3D_USE_BLEND2D (the same condition as
    # command_inspect_text.cpp).
    # Rationale: the corpus depends on text-gated canonical surfaces
    # (scene_presets.hpp → text/text_animator.hpp + content/compositions/
    # chronon_glow_final.hpp text pipeline). In minimal/headless builds
    # (text disabled) the bench corpus is intentionally absent from
    # `chronon3d_cli info`.  P1-7 Chore 1 (commit B) — removed
    # `command_text_audit.cpp` from this comparative list since the
    # file has been deleted wholesale. Cat-3 acceptable
    # trade-off: bench corpus is OPTIONAL tooling, not SDK surface;
    # gated consistently with the rest of the text-required path.
    $<$<AND:$<BOOL:${CHRONON3D_ENABLE_TEXT}>,$<BOOL:${CHRONON3D_USE_BLEND2D}>>:${CMAKE_SOURCE_DIR}/examples/bench_corpus/bench_corpus_scenes.cpp>
    # NO tests/visual/*.cpp here — production CLI does not compile tests/
    # sources (user-spec verbatim "Nella produzione il CLI non compila più
    # sorgenti da tests/").  tests/visual/*.cpp are compiled into the
    # chronon3d_cli_dev sub-target when CHRONON3D_BUILD_CLI_DEV=ON (below).
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
#
# OBJ-lib specifics (chronon3d_sdk singleton via Chronon3D::SDK INTERFACE
# alias; chronon3d_sdk_impl NOT linked directly to preserve the SDK-public-
# surface boundary certified by TICKET-011):
#   chronon3d_sdk_impl is a unity-built OBJECT library aggregating static
#   archives per cmake/Chronon3DRegistry.cmake: chronon3d_pipeline +
#   chronon3d_scene + chronon3d_runtime + chronon3d_cache +
#   chronon3d_effects + chronon3d_registry + chronon3d_assets +
#   chronon3d_extension + chronon3d_animations + chronon3d_text_core +
#   chronon3d_blend2d_paint + chronon3d_graph_* sub-targets.
#   chronon3d_pipeline + chronon3d_scene + chronon3d_runtime ARE NOT
#   linked directly here — they resolve transitively via chronon3d_sdk,
#   which the linker set above resolves to as the Chronon3D::SDK public
#   surface contract entrypoint.
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

# ── TICKET-CLI-ISOLATE-RUNTIME-DEV (2026-07-12) — confirm no tests/visual reference in production CLI ──
# The production CLI no longer compiles any tests/visual/*.cpp.  The
# composition registration is split into 3 layers (register_runtime/
# content/dev_compositions.cpp) — see register_compositions.hpp.  The
# register_dev_compositions.cpp file is empty when CHRONON3D_BUILD_CLI_DEV=OFF
# (production default); tests/visual/*.cpp are compiled only into the
# chronon3d_cli_dev sub-target (see the DEV group above).  The header
# `tests/visual/ae_parity/glow_final_compositions.hpp` was moved to
# `content/compositions/chronon_glow_final.hpp` on 2026-07-12 (predecessor
# refactor(glow) §4 chore) and remains header-only + transitively included
# from CMAKE_SOURCE_DIR for both production runtime + DEV registration.
# This comment is the canonical record satisfying the user-spec verbatim
# "Nella produzione il CLI non compila più sorgenti da tests/" + "tests/visual/
# resta solo come cartella di golden/fixture — nessuna factory registrata da qui
# direttamente" mandates.
