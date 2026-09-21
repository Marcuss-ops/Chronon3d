# The Motion3D canary pack is an independently gated source group: its option,
# its source list and its compile definition live here, like every other gated
# command group in this directory.  The CLI must build byte-for-byte the same
# when the gate is OFF, so nothing outside this file knows the pack exists.
#
# Included by apps/chronon3d_cli/CMakeLists.txt AFTER Executable.cmake, because
# target_sources() needs the chronon3d_cli target to exist.
option(CHRONON3D_CLI_WITH_CANARY_REGISTRY
    "Compile and register the Motion3D canary composition pack" OFF)

if(NOT CHRONON3D_CLI_WITH_CANARY_REGISTRY)
    return()
endif()

# register_canary_compositions.cpp is the pack entry point; the canary/
# subfiles are one family each (motion3d, text contract, Final3D catalog,
# font baselines, Apple V3 matrix, final cards) plus the shared authoring kit
# headers.
target_sources(chronon3d_cli PRIVATE
    register_canary_compositions.cpp
    canary/register_canary_motion3d.cpp
    canary/register_canary_text.cpp
    canary/register_canary_final3d.cpp
    canary/register_canary_font_baselines.cpp
    canary/register_canary_apple_v3.cpp
    canary/register_canary_final_cards.cpp
)
target_compile_definitions(chronon3d_cli PRIVATE
    CHRONON3D_CLI_WITH_CANARY_REGISTRY)
