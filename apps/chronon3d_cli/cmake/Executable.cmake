add_executable(chronon3d_cli
    main.cpp
    register_runtime_compositions.cpp
    register_content_compositions.cpp
    register_dev_compositions.cpp
)

get_target_property(CLI11_INCLUDE_DIRS CLI11::CLI11
    INTERFACE_INCLUDE_DIRECTORIES
)

target_link_libraries(chronon3d_cli PRIVATE
    chronon3d_sdk
    chronon3d_cli_core
    chronon3d_backend_image
    spdlog::spdlog_header_only
    fmt::fmt
)

if(CHRONON3D_ENABLE_TELEMETRY)
    target_compile_definitions(chronon3d_cli_core PRIVATE
        CHRONON3D_ENABLE_SQLITE_TELEMETRY
    )
endif()
if(TARGET chronon3d_c)
    target_link_libraries(chronon3d_cli PRIVATE chronon3d_c)
    target_compile_definitions(chronon3d_cli_core PRIVATE CHRONON3D_HAS_C_API)
endif()
foreach(_target IN ITEMS
    chronon3d_cli_render
    chronon3d_cli_video_export
    chronon3d_cli_dev
    chronon3d_cli_telemetry
)
    if(TARGET ${_target})
        target_link_libraries(chronon3d_cli PRIVATE ${_target})
    endif()
endforeach()
target_link_libraries(chronon3d_cli PRIVATE chronon3d_cli_core)

if(TARGET chronon3d_media_video)
    target_link_libraries(chronon3d_cli PRIVATE chronon3d_media_video)
endif()

if(TARGET chronon3d_cli_render)
    target_compile_definitions(chronon3d_cli PRIVATE CHRONON3D_HAS_CLI_RENDER)
endif()
if(TARGET chronon3d_cli_video_export)
    target_compile_definitions(chronon3d_cli PRIVATE CHRONON3D_HAS_CLI_VIDEO_EXPORT)
endif()
if(TARGET chronon3d_cli_telemetry)
    target_compile_definitions(chronon3d_cli PRIVATE CHRONON3D_HAS_CLI_TELEMETRY)
endif()
if(TARGET chronon3d_cli_dev)
    target_compile_definitions(chronon3d_cli PRIVATE
        CHRONON3D_HAS_CLI_DEV
        CHRONON3D_BUILD_CLI_DEV
    )
endif()

if(TARGET chronon3d_backend_text)
    target_link_libraries(chronon3d_cli PRIVATE chronon3d_backend_text)
endif()
if(CLI11_INCLUDE_DIRS)
    target_include_directories(chronon3d_cli PRIVATE ${CLI11_INCLUDE_DIRS})
endif()
if(CHRONON3D_BUILD_CONTENT OR CHRONON3D_BUILD_DIAGNOSTICS)
    if(TARGET chronon3d_content)
        target_link_libraries(chronon3d_cli PRIVATE chronon3d_content)
    endif()
    target_compile_definitions(chronon3d_cli PRIVATE CHRONON3D_BUILD_CONTENT)
endif()

target_include_directories(chronon3d_cli PRIVATE ${CMAKE_SOURCE_DIR})
target_precompile_headers(chronon3d_cli PRIVATE
    <algorithm>
    <atomic>
    <condition_variable>
    <deque>
    <filesystem>
    <memory>
    <mutex>
    <thread>
    <vector>
    <spdlog/spdlog.h>
    <CLI/CLI.hpp>
)

if(CHRONON3D_ENABLE_TELEMETRY)
    target_link_libraries(chronon3d_cli PRIVATE unofficial::sqlite3::sqlite3)
endif()

set_target_properties(chronon3d_cli_core chronon3d_cli PROPERTIES
    UNITY_BUILD OFF
)
foreach(_target IN ITEMS
    chronon3d_cli_render
    chronon3d_cli_video_export
    chronon3d_cli_dev
    chronon3d_cli_telemetry
)
    if(TARGET ${_target})
        set_target_properties(${_target} PROPERTIES UNITY_BUILD OFF)
    endif()
endforeach()
