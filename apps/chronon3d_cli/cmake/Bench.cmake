if(NOT CHRONON3D_BUILD_CLI_BENCH)
    return()
endif()

add_library(chronon3d_cli_bench STATIC
    commands/group_bench.cpp
    commands/bench/register_bench_commands.cpp
    commands/bench/command_bench.cpp
)
target_include_directories(chronon3d_cli_bench PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli
)
target_link_libraries(chronon3d_cli_bench PRIVATE
    chronon3d_cli_core
    chronon3d_pipeline
    CLI11::CLI11
    spdlog::spdlog_header_only
    fmt::fmt
)
if(TARGET benchmark::benchmark)
    target_link_libraries(chronon3d_cli_bench PUBLIC benchmark::benchmark)
    target_compile_definitions(chronon3d_cli_bench PRIVATE
        CHRONON3D_BUILD_BENCHMARKS
    )
endif()
