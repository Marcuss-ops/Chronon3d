add_library(chronon3d_cli_core STATIC
    commands/group_core.cpp
    commands/watch/register_watch_commands.cpp
    commands/preview/register_preview_commands.cpp
    commands/basic/command_list.cpp
    commands/basic/command_info.cpp
    commands/basic/command_benchmark_machine.cpp
    commands/basic/command_benchmark_saturation.cpp
    commands/daemon/command_daemon.cpp
    daemon/daemon_service.cpp
    utils/common/cli_utils.cpp
    commands/dev/command_doctor_verify.cpp
    commands/dev/doctor_report.cpp
)

target_include_directories(chronon3d_cli_core PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli
)
target_link_libraries(chronon3d_cli_core PRIVATE
    chronon3d_pipeline
    CLI11::CLI11
    spdlog::spdlog_header_only
    fmt::fmt
    nlohmann_json::nlohmann_json
)
if(CHRONON3D_ENABLE_VIDEO AND TARGET chronon3d_media_video)
    target_link_libraries(chronon3d_cli_core PRIVATE chronon3d_media_video)
endif()
target_precompile_headers(chronon3d_cli_core PRIVATE
    <CLI/CLI.hpp>
    <spdlog/spdlog.h>
)
