# ==============================================================================
# src/core/CMakeLists.txt — chronon3d_core_impl (STATIC)
#
# Libreria di base: profiling, benchmark, metriche di sistema, cancellazione,
# clear parallelo del framebuffer.
#
# Dipendenze (PUBLIC):
#   nlohmann_json  — serializzazione metriche benchmark
#   fmt            — formattazione stringhe
#   spdlog         — logging strutturato (header-only)
#
# Fornisce a: chronon3d (INTERFACE), chronon3d_pipeline
# ==============================================================================
add_library(chronon3d_core_impl STATIC
    profiling.cpp
    benchmark_report.cpp
    system_metrics.cpp
    cancellation_token.cpp
    framebuffer_clear_parallel.cpp
)
target_include_directories(chronon3d_core_impl
    PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/deps/include
)
target_link_libraries(chronon3d_core_impl
    PUBLIC nlohmann_json::nlohmann_json fmt::fmt spdlog::spdlog_header_only
)
