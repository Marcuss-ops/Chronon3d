add_executable(chronon3d_cli
    main.cpp
    commands.cpp
)

target_link_libraries(chronon3d_cli
    PRIVATE
        chronon3d_pipeline
        chronon3d_io
        $<$<CXX_COMPILER_ID:MSVC>:-WHOLEARCHIVE:chronon3d_examples_lib>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:chronon3d_examples_lib>
        CLI11::CLI11
        spdlog::spdlog
        fmt::fmt
)
