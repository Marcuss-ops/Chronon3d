# ==============================================================================
# src/runtime/CMakeLists.txt — chronon3d_runtime
# ==============================================================================
#
# SQLite telemetry is optional (CHRONON3D_ENABLE_TELEMETRY).
# When OFF a NullTelemetryStore is compiled instead; when ON the full
# SQLite-backed store is built and CHRONON3D_ENABLE_SQLITE_TELEMETRY
# is propagated to consumers.

set(CHRONON3D_RUNTIME_SOURCES
    telemetry/telemetry_manager.cpp
    renderer_warmup.cpp
    render_preparation.cpp
    render_engine.cpp
    sdk_render_engine.cpp       # P1-A — sdk::RenderEngine PIMPL impl
    # ── P2 motion-graph pre-warm orchestrator (public facade) ──────────────
    motion_graph_prewarm.cpp
    render_runtime.cpp
    gpu_runtime.cpp
    gpu_asset_cache.cpp
    gpu_glyph_atlas.cpp
    gpu_layer_batch.cpp
    # Fixed-slot frame execution primitives. FrameSlotPool owns slot payloads,
    # GpuCompletionTracker owns native GPU protocol/completion state, and
    # FrameQueue owns bounded slot-id handoff. The legacy Ring name is now a
    # compatibility facade composed from these three authorities.
    frame/frame_slot_pool.cpp
    frame/gpu_completion_tracker.cpp
    frame/frame_queue.cpp
    frame/frame_execution_slot_ring.cpp
    device_scheduler.cpp
    overlay_template.cpp
    template_program_cache.cpp   # Fase H (TICKET-VIDEO-COMPILER-ARCH-V1)
    # ── TICKET-011a follow-up #1 — RenderPipeline typed facade over
    #    graph::render_scene_via_graph / graph::render_composition_frame.
    render_pipeline.cpp
    # ── WP-8 follow-up — RenderSession accessor + reset_job bodies
    #    moved out of the runtime/render_session.hpp header so the
    #    header stays free of graph::SceneHasher and SceneProgramStore
    #    full-type includes (TICKET-013 / TICKET-017 boundary).
    render_session.cpp
    # ── TICKET-ASSET-PREP-BARRIER — ResourcePreparation 5-phase
    #    barrier (font-load / image-decode / video-metadata / audio-index
    #    / layout-preparation).  Stateless helper that bridges the gap
    #    between AssetPreflightResolver (path-exists check) and the
    #    actual per-frame render loop; emits a structured
    #    chronon3d::runtime::PreparationError fail-loud BEFORE the
    #    encoder if any required asset cannot be resolved.
    resource_preparation.cpp
    resource_preparation_media.cpp
)

if(CHRONON3D_ENABLE_TELEMETRY)
    list(APPEND CHRONON3D_RUNTIME_SOURCES
        telemetry/sqlite/sqlite_telemetry_store.cpp
        telemetry/sqlite/sqlite_telemetry_store_events.cpp
        telemetry/sqlite/sqlite_telemetry_store_schema.cpp
    )
else()
    list(APPEND CHRONON3D_RUNTIME_SOURCES
        telemetry/null/null_telemetry_store.cpp
    )
endif()

add_library(chronon3d_runtime OBJECT ${CHRONON3D_RUNTIME_SOURCES})
set_target_properties(chronon3d_runtime PROPERTIES UNITY_BUILD OFF)
set_source_files_properties(
    telemetry/sqlite/sqlite_telemetry_store.cpp
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
)
target_link_libraries(chronon3d_runtime PUBLIC
    chronon3d
    chronon3d_graph
    chronon3d_effects
    chronon3d_cache
    chronon3d_backend_software
    chronon3d_backend_image
)
if(CHRONON3D_ENABLE_VIDEO)
    target_link_libraries(chronon3d_runtime PUBLIC chronon3d_media_video)
    target_compile_definitions(chronon3d_runtime PRIVATE CHRONON3D_ENABLE_VIDEO=1)
endif()
if(CHRONON3D_ENABLE_NATIVE_FFMPEG)
    target_link_libraries(chronon3d_runtime PUBLIC chronon3d_ffmpeg_full)
endif()
if(CHRONON3D_ENABLE_CUDA_INTEROP AND CHRONON3D_CUDA_INCLUDE_DIR)
    target_include_directories(chronon3d_runtime PRIVATE "${CHRONON3D_CUDA_INCLUDE_DIR}")
    if(CHRONON3D_CUDA_DRIVER_LIBRARY)
        target_link_libraries(chronon3d_runtime PRIVATE "${CHRONON3D_CUDA_DRIVER_LIBRARY}")
    endif()
endif()
target_include_directories(chronon3d_runtime PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_include_directories(chronon3d_runtime
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include_private
)
target_compile_definitions(chronon3d_runtime PRIVATE
    CHRONON3D_GIT_COMMIT="${CHRONON3D_GIT_COMMIT}"
)
target_precompile_headers(chronon3d_runtime PRIVATE <chronon3d/detail/pch.hpp>)

if(CHRONON3D_ENABLE_TELEMETRY)
    # Generate telemetry schema header from canonical .sql file.
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/telemetry/sqlite/telemetry_schema.sql" TELEMETRY_SCHEMA_CONTENT)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/telemetry/sqlite/telemetry_schema.hpp.in"
        "${CMAKE_CURRENT_BINARY_DIR}/telemetry_schema.hpp"
        @ONLY
    )
    target_include_directories(chronon3d_runtime PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

    target_link_libraries(chronon3d_runtime PUBLIC unofficial::sqlite3::sqlite3)
    target_compile_definitions(chronon3d_runtime PRIVATE CHRONON3D_ENABLE_SQLITE_TELEMETRY)
endif()
