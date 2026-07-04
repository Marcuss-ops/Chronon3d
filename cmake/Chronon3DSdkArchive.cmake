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

# ==============================================================================
# CONTRACT: Single-strategy SDK packaging certified (TICKET-SDK-PACKAGING-CONSOLIDATION)
#   Only ONE aggregated archive is permitted to exist in the build tree:
#   `libchronon3d_sdk_impl.a`.  All per-subsystem OBJECT libraries are
#   aggregated into this single boundary file via CMake >=3.27 native
#   target_link_libraries(STATIC PRIVATE objlib) propagation
#   (cmake/Chronon3DSdkTargets.cmake ¶50 + ¶60).
#   NO legacy OBJECT/SHARED/MODULE variants coexist; the former manual
#   `cmake/sdk_archive_merge.cmake` ar-crs workaround was removed (TICKET-P1-PART2 P1 #12).
#   The consumer-facing link target is `Chronon3D::SDK` ALIAS `chronon3d_sdk`
#   (the only Chronon3D:: namespaced public target -- see Chronon3DSdkTargets.cmake ¶123).
# ==============================================================================

# Defensive configure-time guard: warn if multiple libchronon3d_sdk_*.a files
# would coexist in the build tree.  This is the in-config guard against a
# future regression that re-introduces a SHARED/MODULE/secondary-static
# variant alongside the canonical `libchronon3d_sdk_impl.a`.
file(GLOB _all_sdk_archives "${CMAKE_BINARY_DIR}/src/libchronon3d_sdk_*.a")
list(LENGTH _all_sdk_archives _num_archives)
if(_num_archives GREATER 1)
    # TICKET-SDK-PACKAGING-CONSOLIDATION — build the diagnostic body in a
    # single multi-line string and emit ONE message(FATAL_ERROR ...).
    # An earlier revision used a `foreach(); message(FATAL_ERROR); endforeach()`
    # pattern which the cmake runtime halts after the first iteration, so
    # only the first archive path was reported.  Aggregating the body in a
    # string ensures every offender is enumerated before cmake exits.
    # TICKET-SDK-PACKAGING-CONSOLIDATION — build the diagnostic body in a
    # single multi-line string and emit ONE message(FATAL_ERROR ...).
    # An earlier revision used a `foreach(); message(FATAL_ERROR); endforeach()`
    # pattern which the cmake runtime halts after the first iteration, so
    # only the first archive path was reported.  Aggregating the body in a
    # string ensures every offender is enumerated before cmake exits.
    #   Explicit string(APPEND _err "\n") between every line vs. relying
    # on cmake's `\n`-trailing-line concatenation: the explicit form is
    # invariant under future contributor additions (a 4th line without
    # a trailing `\n` would otherwise silently merge with the previous
    # line in cmake's multi-arg `set()` concatenation).
    set(_err "Chronon3DSdkArchive: MULTIPLE SDK archive files detected.")
    string(APPEND _err "\n")
    string(APPEND _err "  Contract violation (TICKET-SDK-PACKAGING-CONSOLIDATION):")
    string(APPEND _err "\n")
    string(APPEND _err "  only libchronon3d_sdk_impl.a is permitted, found ${_num_archives}:")
    string(APPEND _err "\n")
    foreach(_arch IN LISTS _all_sdk_archives)
        string(APPEND _err "    ${_arch}")
        string(APPEND _err "\n")
    endforeach()
    message(FATAL_ERROR "${_err}")
endif()

# ==============================================================================
# TICKET-GATE-10-AR-RACE-MITIGATION — defensive `sync` before SDK archive creation
# ==============================================================================
# GNU ar 2.45 on ext3/ext4 has a known rename race: `rename(tmp, final)` returns
# 0 but the file appears missing to subsequent reads. ar then reports the
# rename error with `strerror(errno=0)` ("reason: Success") — observed at
# `cmake --build` step [347/347] on `chronon3d_sdk_impl`. Flushing the page
# cache immediately before the archive step eliminates stale entries that
# could interfere with the rename. PRE_LINK on a STATIC library runs BEFORE
# the archive step (per CMake docs) AND CMake skips it when the archive is
# up-to-date — so this only fires on (re)builds, not every incremental build.
# Cat-1 build correction; no public API change. See TICKET-GATE-10-AR-RACE-
# FOLLOWUP for the in-script post-nm `ar t` 0-entries root-cause investigation
# (separate from this build-time mitigation).
find_program(SYNC_EXECUTABLE sync)
if(SYNC_EXECUTABLE AND CMAKE_HOST_UNIX AND NOT CMAKE_CROSSCOMPILING)
    message(STATUS
        "TICKET-GATE-10-AR-RACE-MITIGATION: PRE_LINK `sync` armed for "
        "chronon3d_sdk_impl (mitigates GNU ar 2.45 'reason: Success' "
        "transient on ext3/ext4; only runs when the archive is rebuilt).")
    add_custom_command(TARGET chronon3d_sdk_impl PRE_LINK
        COMMAND "${SYNC_EXECUTABLE}"
        COMMENT "Flushing page cache defensively before SDK archive creation"
        VERBATIM
    )
endif()
