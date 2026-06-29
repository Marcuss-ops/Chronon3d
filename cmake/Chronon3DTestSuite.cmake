# ============================================================================
# cmake/Chronon3DTestSuite.cmake
#
# §12.1 — Test source registration helpers (PR 6.8 / §12 series).
#
# Single source of truth for which test source files participate in
# the project, independent of which CMake target ends up compiling
# them.  The actual `add_executable` + `target_link_libraries` wiring
# is left to the per-area .cmake files (`tests/*_tests.cmake`) for
# now — §11's test tier-separation refactor will consolidate that
# responsibility into this helper in a follow-up commit.
#
# Two functions are exposed, both of which write the canonical
# absolute path of every supplied source to the GLOBAL CMake property
# `CHRONON3D_REGISTERED_TEST_SOURCES`.  The dump step in
# `tests/CMakeLists.txt` (at the very end of configuration) reads
# the property and writes a deterministic list to
# `${CMAKE_BINARY_DIR}/registered_test_sources.txt`, which the
# Python gate (`tools/check_test_source_registration.py`, §12.3)
# diffs against the on-disk `tests/**/test_*.cpp` enumeration.
#
# Why two helpers:
#   * `chronon3d_add_test_suite` captures the high-level intent
#     (test suite = name + tier + sources + link contract) for the
#     §11 forward-compat path.  Today the link contract is captured
#     but NOT enforced; the TIER argument is also captured but NOT
#     enforced (§11 will enforce both).  This is registration-only
#     per §12.1's atomicity contract.
#   * `chronon3d_register_test_source` is the low-level single-source
#     registration helper.  Use it for conditional sources
#     (e.g. text- or blend2d-gated test files) referenced inside
#     `if(... )` blocks where the full-suite metadata (name, tier,
#     link contract) is not available.  Both helpers write to the
#     SAME global property so the dump is a single flat list.
#
# Freeze compliance (AGENTS.md V0.1):
#   * This file lives under `cmake/`, not `include/chronon3d/`, so
#     it does not expand the public API surface.
#   * It introduces no new canonical type or surface — it is build
#     infra only (Category 1 of the freeze: build fixes).
# ============================================================================

# Append each source in ARG_SOURCES to the GLOBAL property
# CHRONON3D_REGISTERED_TEST_SOURCES.  Paths are canonicalised to
# absolute paths so the Python gate (which walks the filesystem
# relative to CMAKE_SOURCE_DIR) and the registered set use the same
# string form.  set_property(GLOBAL APPEND …) creates the property
# on first use, so no pre-declaration is needed.
#
# Captures NAME / TIER / LINK_TARGETS / LABELS for §11 forward-compat
# (these are NOT yet enforced — the per-area .cmake files still do
# their own add_executable + target_link_libraries).  The parse is
# kept (rather than dropped) so call sites that opt into the helper
# in §11 can land their migrations without re-architecting.
function(chronon3d_add_test_suite)
    cmake_parse_arguments(ARG "" "NAME;TIER" "SOURCES;LINK_TARGETS;LABELS" ${ARGN})

    # Fail-fast on missing NAME — a call with no NAME silently registers
    # sources to a property that no consumer can match.  Better to
    # surface the misconfiguration at configure time.
    if(NOT ARG_NAME)
        message(FATAL_ERROR
            "chronon3d_add_test_suite: NAME is required. "
            "Usage: chronon3d_add_test_suite(NAME <name> TIER <U|I|S> "
            "SOURCES <list> [LINK_TARGETS <list>] [LABELS <list>])")
    endif()

    foreach(_source IN LISTS ARG_SOURCES)
        get_filename_component(_absolute "${_source}" ABSOLUTE)
        set_property(
            GLOBAL APPEND
            PROPERTY CHRONON3D_REGISTERED_TEST_SOURCES
            "${_absolute}"
        )
    endforeach()
endfunction()

# Single-source registration helper.  Use for conditional sources
# (e.g. text- or blend2d-gated).  No metadata required; just appends
# each positional argument's source path to the SAME GLOBAL property.
#
# Implementation note: this function does NOT use cmake_parse_arguments
# (no named args needed) — it iterates over all positional arguments
# directly via ARGN.  This keeps the call site trivial:
#   chronon3d_register_test_source(
#       text/test_text_layout.cpp
#       text/test_text_bounds.cpp
#       REQUIRES CHRONON3D_ENABLE_TEXT
#   )
# The `REQUIRES …` tail is ignored by this function (consumed by §11's
# conditional gate) but accepted for forward-compat so call sites
# don't need a §12.2 migration to keep working.
function(chronon3d_register_test_source)
    # §12.1 — STUB.  This function only registers the path; the
    # REQUIRES-gate parsing (filter on which features must be ON for
    # the source to be considered registered) lands in §12.2.  Emit an
    # AUTHOR_WARNING whenever this function is called so callers don't
    # mistake it for the §12.2 final form.  When §12.2 lands, this
    # warning is removed and the function is extended to honour the
    # `REQUIRES <FEATURE_X> <FEATURE_Y>` tail (the tail is already
    # silently accepted by the foreach loop below; the §12.2 commit
    # adds the explicit gate evaluation).
    message(AUTHOR_WARNING
        "chronon3d_register_test_source is a §12.1 stub: REQUIRES-gate "
        "evaluation lands in §12.2.  Current call only registers the "
        "path; the REQUIRES tail (if any) is ignored.  See docs/ §12.2 "
        "for the final form.")

    foreach(_source IN LISTS ARGN)
        # Skip "key=value" pairs (they're metadata for §11's
        # conditional-gate parsing, not source paths).
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
