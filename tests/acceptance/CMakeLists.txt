# ============================================================================
# tests/acceptance/CMakeLists.txt
#
# Forward-points 0a/0b/0c of TICKET-ACCEPTANCE-FORENSIC-SURFACE — minimal
# orchestrator that registers the new acceptance forensic-surface test
# executable (forward-point 0c wiring) into the existing
# `chronon3d_acceptance` aggregate via the `acceptance` CTest label.
#
# This subdirectory exists to avoid polluting the existing per-area
# `.cmake` files (text_definition_tests.cmake, etc.) with a single-test
# insertion. AGENTS.md "Fare PR piccole e mirate" + Cat-3 (zero new
# public API) honoured: 1 helper pair + 1 acceptance test + this
# orchestrator (no SDK surface change).
# ============================================================================

# Forward-point 0c — INTEGRATION tier acceptance test carrying the
# `acceptance` CTest label. Joins the chronon3d_acceptance aggregate
# via the label filter (`ctest -L acceptance`). Cat-3 + Cat-5 freeze
# compliant: no SDK surface change, zero new symbols in include/.
chronon3d_add_test_suite(
    NAME chronon3d_acceptance_forensic_surface_tests
    TIER INTEGRATION
    SOURCES ${CMAKE_CURRENT_LIST_DIR}/test_acceptance_forensic_surface.cpp
    LABELS acceptance
)
