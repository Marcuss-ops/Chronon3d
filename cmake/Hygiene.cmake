# ==============================================================================
# cmake/Hygiene.cmake — Project-level hygiene gates (custom_targets only)
#
# PURPOSE
#   This file hosts CMake custom_targets that act as project-level "gates"
#   rather than library build artifacts.  Kept distinct from
#   cmake/Chronon3DRegistry.cmake, whose contract is "list library names
#   only" (the third-class OBJECT/INTERFACE library registry).  Adding
#   custom_targets to that file would mix two unrelated concerns.
#
# CUSTOM_TARGETS DEFINED HERE
#   check_refs  — invoke tools/check_filename_drift.sh with --strict and
#                 make `cmake --build --target check_refs` the canonical
#                 CI entry point.  Complementary to the R1–R5 co-update
#                 rules in tools/check_doc_sync.sh: this gate enforces
#                 ON-DISK EXISTENCE, not co-update.
#
# USAGE
#   From the top-level CMakeLists.txt (placed alongside the other
#   custom_target block near the bottom, after add_subdirectory() so any
#   test gates are ready):
#     include(cmake/Hygiene.cmake)
#   Then:
#     cmake --build <build-dir> --target check_refs
#
# Wire-in note: this target is registered UNCONDITIONALLY so CI / manual
# invocation works regardless of CHRONON3D_BUILD_TESTS / BUILD_CLI / etc.
# The shell script is the source of truth for what "valid" means.
# ==============================================================================

add_custom_target(check_refs
    COMMAND "${CMAKE_SOURCE_DIR}/tools/check_filename_drift.sh" --strict
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Verify filename citations resolve on disk (tools/check_filename_drift.sh)"
    VERBATIM
)
