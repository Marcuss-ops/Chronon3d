add_executable(chronon3d_cli
    main.cpp
)

target_link_libraries(chronon3d_cli
    PRIVATE
        chronon3d
        spdlog::spdlog
        fmt::fmt
        Tracy::TracyClient
        absl::flags
        absl::flags_parse
        absl::flags_usage
        absl::flags_config
        absl::base
        absl::raw_logging_internal
        mimalloc
)

# Optional: Add Tracy instrumentation if needed
# target_compile_definitions(chronon3d_cli PRIVATE TRACY_ENABLE)
