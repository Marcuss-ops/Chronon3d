# ==============================================================================
# src/runtime/CMakeLists.txt — chronon3d_runtime
# ==============================================================================

set(CHRONON3D_RUNTIME_SOURCES
    bench_runner.cpp
    pipeline.cpp
    scene_to_render_graph.cpp
    timeline_evaluator.cpp
    sub_composition_registry.cpp
    telemetry/telemetry_manager.cpp
    renderer_warmup.cpp
)
if(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
    list(APPEND CHRONON3D_RUNTIME_SOURCES
        telemetry/sqlite/sqlite_telemetry_store.cpp
        telemetry/sqlite/sqlite_telemetry_store_schema.cpp
    )
else()
    list(APPEND CHRONON3D_RUNTIME_SOURCES
        telemetry/sqlite/sqlite_telemetry_store_stub.cpp
    )
endif()

add_library(chronon3d_runtime STATIC ${CHRONON3D_RUNTIME_SOURCES})
target_link_libraries(chronon3d_runtime PUBLIC
    chronon3d
    chronon3d_graph
    chronon3d_effects
    chronon3d_cache
    Taskflow::Taskflow
)
target_include_directories(chronon3d_runtime PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_precompile_headers(chronon3d_runtime PRIVATE <chronon3d/detail/pch.hpp>)

# Generate telemetry schema header from canonical .sql file.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/telemetry/sqlite/telemetry_schema.sql" TELEMETRY_SCHEMA_CONTENT)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/telemetry/sqlite/telemetry_schema.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/telemetry_schema.hpp"
    @ONLY
)
target_include_directories(chronon3d_runtime PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

if(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
    target_link_libraries(chronon3d_runtime PUBLIC unofficial::sqlite3::sqlite3)
    target_compile_definitions(chronon3d_runtime PUBLIC CHRONON3D_ENABLE_SQLITE_TELEMETRY)
endif()
