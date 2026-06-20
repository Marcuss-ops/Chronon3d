# ==============================================================================
# src/expressions/CMakeLists.txt
# ==============================================================================
#
# Path B expression engine lives at v2/. The v1 (Path A) inline parser lives
# inside AnimatedValue — no separate target. Only v2 ships separately, so we
# only add_subdirectory(v2).

add_subdirectory(v2)
