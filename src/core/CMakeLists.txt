# ==============================================================================
# src/core/CMakeLists.txt — chronon3d_core_impl
# ==============================================================================

add_library(chronon3d_core_impl STATIC
    config.cpp
    profiling.cpp
    benchmark_report.cpp
    system_metrics.cpp
    cancellation_token.cpp
    framebuffer_clear_parallel.cpp
    simd_core_kernels.cpp
    camera_2_5d_log.cpp
    memory/framebuffer.cpp
)
target_include_directories(chronon3d_core_impl PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(chronon3d_core_impl PUBLIC
    nlohmann_json::nlohmann_json
    fmt::fmt
    spdlog::spdlog_header_only
)
