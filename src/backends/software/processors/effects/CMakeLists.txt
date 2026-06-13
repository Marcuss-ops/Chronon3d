# ==============================================================================
# src/backends/software/processors/effects/CMakeLists.txt
# ==============================================================================
#
# Effect processor subdirectories.  Each follow-up agent adds its files to the
# appropriate subdirectory below and adds a target_sources call here.
#
#   A2/A3 → color/
#   A4    → generate/
#   A5/A6 → blur/
#   A7    → stroke/

add_subdirectory(color)
add_subdirectory(generate)
add_subdirectory(blur)
add_subdirectory(stroke)
