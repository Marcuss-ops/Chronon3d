# ==============================================================================
# src/core/CMakeLists.txt — chronon3d_core_impl
#
# PUBLIC include dirs must use generator expressions so the installed
# Chronon3DTargets.cmake substitutes the install-time path (`include/`)
# instead of leaking the source-tree path.
# ==============================================================================

add_library(chronon3d_core_impl OBJECT
    config.cpp
    profiling.cpp
    benchmark_report.cpp
    system_metrics.cpp
    cancellation_token.cpp
    framebuffer_clear_parallel.cpp
    simd_core_kernels.cpp
    camera_2_5d_log.cpp
    memory/framebuffer.cpp
    scheduler/execution_scheduler.cpp      # PR-B: ExecutionScheduler owns the tbb::task_arena
    scheduler/scheduler_mode.cpp           # PR-B: SchedulerMode enum + parser, no TBB dep
)
target_include_directories(chronon3d_core_impl PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(chronon3d_core_impl PUBLIC
    nlohmann_json::nlohmann_json
    fmt::fmt
    spdlog::spdlog_header_only
)
target_link_libraries(chronon3d_core_impl PUBLIC TBB::tbb)   # tbb::task_arena in execution_scheduler.hpp
