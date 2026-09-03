# SDK archive configuration hooks.
# CMake >= 3.27 aggregates registered OBJECT libraries directly into the
# canonical chronon3d_sdk_impl STATIC archive; archive correctness is verified
# after build by tools/sdk/check_archive_canaries.sh.

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/cmake/Chronon3DCanarySymbols.cmake")
    message(FATAL_ERROR
        "Chronon3DSdkArchive: missing cmake/Chronon3DCanarySymbols.cmake")
endif()

# Defensive mitigation for the GNU ar transient historically observed on the
# Linux verification host. PRE_LINK runs only when the archive is rebuilt.
find_program(SYNC_EXECUTABLE sync)
if(SYNC_EXECUTABLE AND CMAKE_HOST_UNIX AND NOT CMAKE_CROSSCOMPILING)
    add_custom_command(TARGET chronon3d_sdk_impl PRE_LINK
        COMMAND "${SYNC_EXECUTABLE}"
        COMMENT "Flushing filesystem state before SDK archive creation"
        VERBATIM
    )
endif()
