# ==============================================================================
# cmake/Chronon3DSdkArchive.cmake — SDK archive canary guard
#
# PURPOSE
#   Defensive configure-time check: the canary symbols catalog must be
#   present so tools/install_consumer_test.sh Fase-4 gate can verify
#   the SDK archive contains expected symbols.
#
#   CMake ≥3.27 natively aggregates OBJECT .o files into STATIC archives
#   via target_link_libraries(STATIC PRIVATE objlib) — the manual `ar crs`
#   workaround (formerly `cmake/sdk_archive_merge.cmake` + `sdk_archive_merge`
#   custom target) has been removed (TICKET-P1-PART2, P1 #12).
#
# INCLUDED FROM
#   cmake/Chronon3DSdkTargets.cmake — after `add_library(chronon3d_sdk_impl …)`
#   and the registry-driven foreach target_link_libraries block.
# ==============================================================================

# ── Optional sanity: the canary catalog is consumed at install time by
#    tools/install_consumer_test.sh.  Verify the catalog file is present
#    at configure time so a missing file is surfaced loud before CI runs.
#    The catalog IS NOT consumed by configure-time logic; this check is
#    defensive documentation only.
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/cmake/Chronon3DCanarySymbols.cmake")
    message(WARNING
        "Chronon3DSdkArchive: canary catalog missing at "
        "${CMAKE_SOURCE_DIR}/cmake/Chronon3DCanarySymbols.cmake. "
        "tools/install_consumer_test.sh Fase-4 gate will fail loud.")
endif()
