# ==============================================================================
# src/runtime/CMakeLists.txt — chronon3d_runtime
# ==============================================================================
#
# Wave 1D (SQLite demolition): no database/persistence layer in the engine.
# The runtime keeps an events-only telemetry boundary (NullTelemetryStore
# default); hosts/collectors observe Chronon out-of-band. No SQLite dependency.

set(CHRONON3D_RUNTIME_SOURCES
    telemetry/telemetry_manager.cpp
    renderer_warmup.cpp
    render_preparation.cpp
    render_engine.cpp
    sdk_render_engine.cpp       # P1-A — sdk::RenderEngine PIMPL impl
    # ── P2 motion-graph pre-warm orchestrator (public facade) ──────────────
    motion_graph_prewarm.cpp
    render_runtime.cpp
    context/runtime_context.cpp
    job/job_session.cpp
    job/render_job_normalizer.cpp
    frame/frame_execution.cpp
    gpu_runtime.cpp
    gpu_asset_cache.cpp
    gpu_glyph_atlas.cpp
    gpu_layer_batch.cpp
    # Fixed-slot frame execution authorities: FrameSlotPool owns slot payloads,
    # GpuCompletionTracker owns native GPU protocol/completion state, and
    # FrameQueue owns bounded slot-id handoff.
    frame/frame_slot_pool.cpp
    frame/gpu_completion_tracker.cpp
    frame/frame_queue.cpp
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
    telemetry/null/null_telemetry_store.cpp
)

add_library(chronon3d_runtime OBJECT ${CHRONON3D_RUNTIME_SOURCES})
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
