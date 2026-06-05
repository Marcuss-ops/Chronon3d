# ── Benchmarks (only built when explicitly enabled and the package is available) ──

if(CHRONON3D_BUILD_BENCHMARKS AND TARGET benchmark::benchmark_main)
    add_executable(chronon3d_benchmarks
        bench/micro_benchmarks.cpp
        bench/dof_benchmark.cpp
    )
    target_link_libraries(chronon3d_benchmarks
        PRIVATE
            chronon3d_pipeline
            benchmark::benchmark_main
    )
    target_include_directories(chronon3d_benchmarks PRIVATE ${CMAKE_SOURCE_DIR})
endif()
