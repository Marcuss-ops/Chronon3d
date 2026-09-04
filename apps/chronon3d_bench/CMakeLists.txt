# =============================================================================
# apps/chronon3d_bench/CMakeLists.txt — standalone bench binary.
# =============================================================================

option(CHRONON3D_BUILD_BENCHMARK_CLI
    "Build chronon3d_bench standalone binary (bench command only)" OFF)

if(NOT CHRONON3D_BUILD_BENCHMARK_CLI)
    return()
endif()

if(NOT CHRONON3D_BUILD_CLI)
    message(STATUS
        "CHRONON3D_BUILD_BENCHMARK_CLI=ON with CHRONON3D_BUILD_CLI=OFF: "
        "chronon3d_bench will be a self-contained executable.")
endif()

add_executable(chronon3d_bench
    main.cpp
    cmd/command_bench_support.cpp
    cmd/command_bench_run.cpp
    cmd/register_bench_commands.cpp
)

target_include_directories(chronon3d_bench PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/apps/chronon3d_bench
)

target_link_libraries(chronon3d_bench PRIVATE
    chronon3d_sdk
    chronon3d_pipeline
    chronon3d_backend_image
    CLI11::CLI11
    spdlog::spdlog_header_only
    fmt::fmt
)

if(TARGET benchmark::benchmark)
    target_link_libraries(chronon3d_bench PRIVATE benchmark::benchmark)
    target_compile_definitions(chronon3d_bench PRIVATE
        CHRONON3D_BUILD_BENCHMARKS
    )
endif()

set_target_properties(chronon3d_bench PROPERTIES UNITY_BUILD OFF)
