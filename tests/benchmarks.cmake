# ── Benchmarks (only built when explicitly enabled and the package is available) ──

if(CHRONON3D_BUILD_BENCHMARKS AND TARGET benchmark::benchmark_main)
    add_executable(chronon3d_benchmarks
        bench/micro_benchmarks.cpp
        # bench/dof_benchmark.cpp      # DISABLED: pre-existing API bit-rot (lens parameter drift on apply_disc_dof)
        # bench/bench_blend_modes.cpp  # DISABLED: pre-existing API bit-rot (chronon3d::simd namespace drift on composite_*)
    )
    target_link_libraries(chronon3d_benchmarks
        PRIVATE
            chronon3d_pipeline
            chronon3d_graph_cache
            benchmark::benchmark_main
    )
    target_include_directories(chronon3d_benchmarks PRIVATE ${CMAKE_SOURCE_DIR})
    add_test(NAME chronon3d_benchmarks COMMAND chronon3d_benchmarks --benchmark_min_time=0.01 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    # Scene program benchmark (separate target due to different dependencies)
    add_executable(chronon3d_scene_program_benchmarks
        bench/bench_scene_program.cpp
    )
    target_link_libraries(chronon3d_scene_program_benchmarks
        PRIVATE
            chronon3d_pipeline
            chronon3d_graph_cache
            benchmark::benchmark_main
    )
    target_include_directories(chronon3d_scene_program_benchmarks PRIVATE ${CMAKE_SOURCE_DIR})
    add_test(NAME chronon3d_scene_program_benchmarks COMMAND chronon3d_scene_program_benchmarks --benchmark_min_time=0.01 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
