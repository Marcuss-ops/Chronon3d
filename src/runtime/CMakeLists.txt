# ==============================================================================
# src/runtime/CMakeLists.txt — chronon3d_runtime
# ==============================================================================
#
# SQLite telemetry is always-on (June 2026 lean cleanup): stub-mode impl was
# dropped, so we always compile the real sources and always link sqlite3.
# `CHRONON3D_ENABLE_SQLITE_TELEMETRY` is defined unconditionally at the
# project root.

set(CHRONON3D_RUNTIME_SOURCES
    telemetry/telemetry_manager.cpp
    telemetry/sqlite/sqlite_telemetry_store.cpp
    telemetry/sqlite/sqlite_telemetry_store_schema.cpp
    renderer_warmup.cpp
)

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

target_link_libraries(chronon3d_runtime PUBLIC unofficial::sqlite3::sqlite3)
target_compile_definitions(chronon3d_runtime PUBLIC CHRONON3D_ENABLE_SQLITE_TELEMETRY)
