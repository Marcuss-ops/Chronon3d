add_executable(chronon3d_cli
    main.cpp
    commands.cpp
)

target_link_libraries(chronon3d_cli
    PRIVATE
        chronon3d_pipeline
        chronon3d_io
        CLI11::CLI11
        tomlplusplus::tomlplusplus
        spdlog::spdlog
        fmt::fmt
)
