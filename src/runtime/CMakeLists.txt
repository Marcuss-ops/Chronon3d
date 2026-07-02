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
    render_engine.cpp
    # ── P2 motion-graph pre-warm orchestrator (public facade) ──────────────
    motion_graph_prewarm.cpp
    render_runtime.cpp
    # ── TICKET-011a follow-up #1 — RenderPipeline typed facade over
    #    graph::render_scene_via_graph / graph::render_composition_frame.
    render_pipeline.cpp
    # ── WP-8 follow-up — RenderSession accessor + reset_job bodies
    #    moved out of the runtime/render_session.hpp header so the
    #    header stays free of graph::SceneHasher and SceneProgramStore
    #    full-type includes (TICKET-013 / TICKET-017 boundary).
    render_session.cpp
)

if(CHRONON3D_ENABLE_TELEMETRY)
    list(APPEND CHRONON3D_RUNTIME_SOURCES
        telemetry/sqlite/sqlite_telemetry_store.cpp
        telemetry/sqlite/sqlite_telemetry_store_schema.cpp
    )
else()
    list(APPEND CHRONON3D_RUNTIME_SOURCES
        telemetry/null/null_telemetry_store.cpp
    )
endif()

add_library(chronon3d_runtime OBJECT ${CHRONON3D_RUNTIME_SOURCES})
target_link_libraries(chronon3d_runtime PUBLIC
    chronon3d
    chronon3d_graph
    chronon3d_effects
    chronon3d_cache
    chronon3d_backend_software
    chronon3d_backend_image
)
target_include_directories(chronon3d_runtime PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_include_directories(chronon3d_runtime
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include_private
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
    target_compile_definitions(chronon3d_runtime PUBLIC CHRONON3D_ENABLE_SQLITE_TELEMETRY)
endif()
