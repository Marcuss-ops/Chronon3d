# Chronon3D CLI build bootstrap. Each independently gated command group owns
# its source list and link closure in apps/chronon3d_cli/cmake/.

option(CHRONON3D_BUILD_CLI_CORE      "Build the core CLI commands" ON)
option(CHRONON3D_BUILD_CLI_RENDER    "Build the canonical render command" ON)
option(CHRONON3D_BUILD_CLI_TELEMETRY "Build telemetry commands" ON)
option(CHRONON3D_BUILD_CLI_DEV       "Build developer commands" OFF)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Core.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Render.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/VideoExport.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Telemetry.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Dev.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Executable.cmake)
