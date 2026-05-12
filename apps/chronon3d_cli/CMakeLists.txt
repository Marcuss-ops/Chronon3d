add_executable(chronon3d_cli
    main.cpp
)

target_link_libraries(chronon3d_cli
    PRIVATE
        chronon3d
        chronon3d_pipeline
        absl::flags
        absl::flags_parse
        absl::flags_usage
)
