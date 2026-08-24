# ==============================================================================
# src/core/CMakeLists.txt — chronon3d_core_impl
#
# PUBLIC include dirs must use generator expressions so the installed
# Chronon3DTargets.cmake substitutes the install-time path (`include/`)
# instead of leaking the source-tree path.
# ==============================================================================

set(CHRONON3D_CORE_IMPL_SOURCES
    config.cpp
    cpu_budget.cpp
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
    tracing/perfetto_backend.cpp           # Perfetto SDK once-per-process bootstrap
    tracing/trace_session.cpp              # job-scoped TraceSession (pimpl)
    tracing/tracing_categories.cpp         # PERFETTO_TRACK_EVENT_STATIC_STORAGE (single TU)
)

if(CHRONON3D_ENABLE_CRASH_HANDLER)
    list(APPEND CHRONON3D_CORE_IMPL_SOURCES crash/crash_handler.cpp)
endif()
if(CHRONON3D_ENABLE_DEV_WATCH)
    list(APPEND CHRONON3D_CORE_IMPL_SOURCES dev/watch_service.cpp)
endif()
if(CHRONON3D_ENABLE_OCIO)
    list(APPEND CHRONON3D_CORE_IMPL_SOURCES color/ocio_color_transform.cpp)
endif()

add_library(chronon3d_core_impl OBJECT ${CHRONON3D_CORE_IMPL_SOURCES})
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

if(CHRONON3D_ENABLE_CRASH_HANDLER)
    target_link_libraries(chronon3d_core_impl PUBLIC cpptrace::cpptrace)
    # PUBLIC so consumers (daemon, CLI) see the feature guard consistently.
    target_compile_definitions(chronon3d_core_impl PUBLIC
        CHRONON3D_ENABLE_CRASH_HANDLER
    )
    # GIT_COMMIT stays PRIVATE — only crash_handler.cpp consumes it.
    target_compile_definitions(chronon3d_core_impl PRIVATE
        CHRONON3D_GIT_COMMIT="${CHRONON3D_GIT_COMMIT}"
    )
endif()
if(CHRONON3D_ENABLE_DEV_WATCH)
    target_link_libraries(chronon3d_core_impl PUBLIC efsw::efsw)
    target_compile_definitions(chronon3d_core_impl PUBLIC
        CHRONON3D_ENABLE_DEV_WATCH
    )
endif()
if(CHRONON3D_ENABLE_OCIO)
    target_link_libraries(chronon3d_core_impl PUBLIC OpenColorIO::OpenColorIO)
    target_compile_definitions(chronon3d_core_impl PUBLIC CHRONON3D_ENABLE_OCIO)
endif()
