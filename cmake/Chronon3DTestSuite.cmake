# ============================================================================
# cmake/Chronon3DTestSuite.cmake
#
# §11.1 + §12.1 — Test suite registration + link-contract helper.
#
# Single source of truth for test source registration AND per-tier
# link contract.  Replaces the per-area .cmake file boilerplate
# (`add_executable(<name> ${TEST_MAIN} <list of sources>)` +
# `target_link_libraries(<name> PRIVATE chronon3d_sdk
# chronon3d_sdk_impl chronon3d_pipeline doctest::doctest)`) with
# one typed call site.
#
# Two functions are exposed:
#
#   * chronon3d_add_test_suite(NAME <name> TIER <UNIT|INTEGRATION|SDK>
#                              SOURCES <list> [LINK_TARGETS <list>]
#                              [LABELS <list>])
#     — Registers sources to the §12.3 GLOBAL property
#       CHRONON3D_REGISTERED_TEST_SOURCES.
#     — Validates TIER against the canonical enum.
#     — Derives the default link contract from TIER (overridable via
#       LINK_TARGETS; emits AUTHOR_WARNING when overridden).
#     — Emits add_executable(${NAME} ${TEST_MAIN} ${SOURCES}).
#     — Emits target_link_libraries(${NAME} PRIVATE
#       ${LINK_TARGETS} doctest::doctest).
#     — Sets include dirs directly (replaces what chronon3d_sdk
#       INTERFACE used to provide for in-tree tests; internal tests
#       no longer link chronon3d_sdk per §11.3).
#     — Calls chronon3d_enable_test_pch.
#     — Emits add_test(NAME ${NAME} COMMAND ${NAME}
#       WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}).
#     — Tracks the target to per-tier + global GLOBAL properties
#       for the §11.4 gate.
#
#   * chronon3d_register_test_source(...)
#     — Single-source registration helper (§12.1 forward-compat
#       stub; §12.2 extends with REQUIRES-gate evaluation).  No
#       TIER, no add_executable emission.  Used for conditional
#       sources that don't have a single test-suite context.
#
# TIER semantics (canonical enum):
#
#   UNIT         — internal types/methods, no rendering backend.
#                  Link contract: chronon3d_pipeline (the OBJECT
#                  aggregate of every per-subsystem .o).  No
#                  rendering, no SDK.
#   INTEGRATION  — end-to-end pipeline tests, may render.
#                  Link contract: chronon3d_pipeline +
#                  chronon3d_backend_software.  No SDK.
#   SDK          — only the archive consumer test (the
#                  tests/package_consumer/ executable, plus any
#                  future find_package(Chronon3D) consumer tests).
#                  Link contract: chronon3d_sdk (the INTERFACE
#                  that resolves to libchronon3d_sdk_impl.a at
#                  install time).
#
# Forward-compat: LINK_TARGETS override is supported for the rare
# test that needs custom third-party deps (e.g. chronon3d_backend_text
# for Blend2D-gated text tests).  AUTHOR_WARNING is emitted on
# override; the override path is not the default.
#
# Why a per-tier tracker:
#   The §11.4 gate (tools/check_arch_test_tiers.sh) needs to know
#   which targets belong to which tier so it can fail on any
#   internal test that links chronon3d_sdk / chronon3d_sdk_impl.
#   The helper writes each target name to GLOBAL properties
#   CHRONON3D_TIER_UNIT / CHRONON3D_TIER_INTEGRATION / CHRONON3D_TIER_SDK
#   AND a flat list CHRONON3D_ALL_TEST_TARGETS.
#
# Freeze compliance (AGENTS.md V0.1):
#   * This file lives under `cmake/`, not `include/chronon3d/`, so
#     it does not expand the public API surface.
#   * CATEGORY 1 (build fixes) + CATEGORY 3 (legacy removal of
#     inconsistent per-area link patterns).  The §11.3 commit will
#     remove the `chronon3d_sdk chronon3d_sdk_impl` from internal
#     test targets; §11.4 will gate the new contract.
# ============================================================================

# Canonical TIER enum + per-tier default link contract.
# The default link contract is overridable via LINK_TARGETS, but the
# override path emits an AUTHOR_WARNING.  Internal test executables
# MUST use one of these three tiers; the §11.4 gate enforces the
# absence of chronon3d_sdk / chronon3d_sdk_impl in UNIT/INTEGRATION
# test executables.
set(_CHRONON3D_TIER_DEFAULT_LINKS_UNIT       "chronon3d_pipeline")
set(_CHRONON3D_TIER_DEFAULT_LINKS_INTEGRATION "chronon3d_pipeline;chronon3d_backend_software")
set(_CHRONON3D_TIER_DEFAULT_LINKS_SDK        "chronon3d_sdk")

# Register the test suite + emit the executable + link + add_test.
# See header comment for the full contract.
function(chronon3d_add_test_suite)
    cmake_parse_arguments(ARG "" "NAME;TIER" "SOURCES;LINK_TARGETS;LABELS" ${ARGN})

    # Fail-fast: NAME is the add_executable target name; a call with
    # no NAME silently registers sources to a property no consumer
    # can match.  Surface the misconfiguration at configure time.
    if(NOT ARG_NAME)
        message(FATAL_ERROR
            "chronon3d_add_test_suite: NAME is required. "
            "Usage: chronon3d_add_test_suite(NAME <name> "
            "TIER <UNIT|INTEGRATION|SDK> SOURCES <list> "
            "[LINK_TARGETS <list>] [LABELS <list>])")
    endif()

    # Fail-fast: TIER is the §11.1 contract discriminant.  Without
    # it, the helper cannot derive the default link contract, and
    # the §11.4 gate cannot classify the target.
    if(NOT ARG_TIER)
        message(FATAL_ERROR
            "chronon3d_add_test_suite: TIER is required for '${ARG_NAME}'. "
            "Must be one of: UNIT  INTEGRATION  SDK")
    endif()

    # Fail-fast: TIER must be one of the canonical enum values.
    if(NOT ARG_TIER STREQUAL "UNIT" AND
       NOT ARG_TIER STREQUAL "INTEGRATION" AND
       NOT ARG_TIER STREQUAL "SDK")
        message(FATAL_ERROR
            "chronon3d_add_test_suite: TIER='${ARG_TIER}' is invalid for "
            "'${ARG_NAME}'.  Must be one of: UNIT  INTEGRATION  SDK")
    endif()

    # Derive the default link contract from the tier (unless caller
    # explicitly overrode via LINK_TARGETS).  The override path is
    # for the rare test that needs custom third-party deps
    # (e.g. chronon3d_backend_text for Blend2D-gated tests); it
    # emits an AUTHOR_WARNING so the call site is auditable.
    if(NOT ARG_LINK_TARGETS)
        set(ARG_LINK_TARGETS "${_CHRONON3D_TIER_DEFAULT_LINKS_${ARG_TIER}}")
    else()
        message(AUTHOR_WARNING
            "chronon3d_add_test_suite: LINK_TARGETS override on "
            "'${ARG_NAME}' (tier='${ARG_TIER}').  Default would have been: "
            "${_CHRONON3D_TIER_DEFAULT_LINKS_${ARG_TIER}}.  Override is "
            "RARE — confirm this is intentional (e.g. for tests with "
            "custom third-party deps like chronon3d_backend_text).")
    endif()

    # Register sources to the GLOBAL property (preserves §12.1
    # behavior for the §12.3 gate).  Paths are canonicalised to
    # absolute paths so the Python gate (which walks the filesystem
    # relative to CMAKE_SOURCE_DIR) and the registered set use the
    # same string form.
    foreach(_source IN LISTS ARG_SOURCES)
        get_filename_component(_absolute "${_source}" ABSOLUTE)
        set_property(
            GLOBAL APPEND
            PROPERTY CHRONON3D_REGISTERED_TEST_SOURCES
            "${_absolute}"
        )
    endforeach()

    # ── NEW in §11.1: emit add_executable + target_link_libraries ────
    # Replaces the per-area .cmake boilerplate:
    #   add_executable(${NAME} ${TEST_MAIN} <list of sources>)
    #   target_link_libraries(${NAME} PRIVATE <links> doctest::doctest)
    #   target_include_directories(${NAME} PRIVATE ${CMAKE_SOURCE_DIR} ...)
    #   chronon3d_enable_test_pch(${NAME})
    #   add_test(NAME ${NAME} COMMAND ${NAME} WORKING_DIRECTORY ...)
    add_executable(${ARG_NAME} ${TEST_MAIN} ${ARG_SOURCES})

    # M1.5#7 — UNITY_BUILD OFF for tests
    # Tests frequently define global-scope helpers (e.g. fixture_exists,
    # skip_if_missing, inter_bold, make_real_shape_for_test) with the
    # SAME signature across multiple files.  When unity build merges
    # them into a single TU, these collide as redefinition errors.
    # Whole-project unity build remains ON for source libraries, so
    # compile time is only affected for test executables.
    set_target_properties(${ARG_NAME} PROPERTIES UNITY_BUILD OFF)

    target_link_libraries(${ARG_NAME} PRIVATE
        ${ARG_LINK_TARGETS}
        doctest::doctest
    )
    # Include dirs: previously provided by `chronon3d_sdk` INTERFACE's
    # $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>.  After §11.3 the
    # internal test executables do NOT link chronon3d_sdk, so the
    # helper must set the include dirs explicitly.  Both
    # ${CMAKE_SOURCE_DIR}/include (public chronon3d headers) and
    # ${CMAKE_SOURCE_DIR}/tests (test-only headers like
    # tests/helpers/test_utils.hpp) are required by existing tests.
    target_include_directories(${ARG_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}
    )
    chronon3d_enable_test_pch(${ARG_NAME})

    add_test(NAME ${ARG_NAME}
        COMMAND ${ARG_NAME}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    # LABELS is forwarded to ctest's -L filter (e.g. `ctest -L gate`).
    # Must be set AFTER add_test so the test target exists.
    if(ARG_LABELS)
        set_tests_properties(${ARG_NAME} PROPERTIES LABELS "${ARG_LABELS}")
    endif()

    # ── NEW in §11.1: per-tier + global target tracking for §11.4 ───
    # The §11.4 gate (tools/check_arch_test_tiers.sh) enumerates
    # these properties to know which targets belong to which tier
    # so it can fail on any internal test that links chronon3d_sdk
    # or chronon3d_sdk_impl.
    set_property(
        GLOBAL APPEND
        PROPERTY CHRONON3D_TIER_${ARG_TIER}
        "${ARG_NAME}"
    )
    set_property(
        GLOBAL APPEND
        PROPERTY CHRONON3D_ALL_TEST_TARGETS
        "${ARG_NAME}"
    )
endfunction()

# Single-source registration helper.  Use for conditional sources
# (e.g. text- or blend2d-gated).  No TIER, no add_executable — just
# appends each positional argument's source path to the §12.3
# GLOBAL property.
#
# §12.1 — STUB.  The REQUIRES-gate parsing (filter on which features
# must be ON for the source to be considered registered) lands in
# §12.2.  Emit an AUTHOR_WARNING whenever this function is called so
# callers don't mistake it for the §12.2 final form.
function(chronon3d_register_test_source)
    message(AUTHOR_WARNING
        "chronon3d_register_test_source is a §12.1 stub: REQUIRES-gate "
        "evaluation lands in §12.2.  Current call only registers the "
        "path; the REQUIRES tail (if any) is ignored.  See docs/ §12.2 "
        "for the final form.")

    foreach(_source IN LISTS ARGN)
        # Skip "key=value" pairs (metadata for §11/§12 conditional
        # gate parsing, not source paths).
        if(_source MATCHES "^[A-Z_][A-Z0-9_]*=")
            continue()
        endif()
        get_filename_component(_absolute "${_source}" ABSOLUTE)
        set_property(
            GLOBAL APPEND
            PROPERTY CHRONON3D_REGISTERED_TEST_SOURCES
            "${_absolute}"
        )
    endforeach()
endfunction()
