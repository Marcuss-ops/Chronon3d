add_executable(chronon3d_cli
    main.cpp
    commands.cpp
    proof_suites.cpp
)

target_link_libraries(chronon3d_cli
    PRIVATE
        chronon3d_pipeline
        chronon3d_io
        $<$<BOOL:${CHRONON3D_BUILD_EXAMPLES}>:$<LINK_LIBRARY:WHOLE_ARCHIVE,chronon3d_examples_lib>>
        CLI11::CLI11
        tomlplusplus::tomlplusplus
        spdlog::spdlog
        fmt::fmt
)
