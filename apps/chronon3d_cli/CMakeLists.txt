add_executable(chronon3d_cli
    main.cpp
    commands.cpp
    ../../examples/foundation_proof.cpp
)

target_link_libraries(chronon3d_cli
    PRIVATE
        chronon3d
        chronon3d_pipeline
        chronon3d_io
        CLI11::CLI11
)
