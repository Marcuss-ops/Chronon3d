if(NOT (CHRONON3D_ENABLE_TELEMETRY AND CHRONON3D_BUILD_CLI_TELEMETRY))
    return()
endif()

add_library(chronon3d_cli_telemetry STATIC
    commands/group_telemetry.cpp
    commands/telemetry/register_telemetry_commands.cpp
    commands/telemetry/command_telemetry.cpp
    commands/telemetry/command_telemetry_helpers.cpp
    commands/telemetry/command_telemetry_summary.cpp
)
target_include_directories(chronon3d_cli_telemetry PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli
)
target_link_libraries(chronon3d_cli_telemetry PRIVATE
    chronon3d_cli_core
    chronon3d_pipeline
    CLI11::CLI11
    spdlog::spdlog_header_only
    fmt::fmt
    unofficial::sqlite3::sqlite3
)
target_compile_definitions(chronon3d_cli_telemetry PUBLIC
    CHRONON3D_ENABLE_SQLITE_TELEMETRY
)
