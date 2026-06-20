# ==============================================================================
# experimental/expressions/CMakeLists.txt — Path-B expression engine (QUARANTINE)
# ==============================================================================
#
# This subtree is EXPERIMENTAL: it is gated by CHRONON3D_BUILD_EXPERIMENTAL=OFF by
# default from the root CMakeLists.txt. While quarantined:
#   * Headers are NOT in the SDK public include path (they live under
#     include/chronon3d_experimental/ in this subtree, NOT under
#     include/chronon3d/).
#   * The chronon3d_expressions_v2 library target is NOT listed in
#     _chronon3d_install_targets, so it is NOT installed.
#   * It is NOT a transitive dependency of chronon3d_sdk / Chronon3D::SDK.
#   * It is NOT included in the umbrella headers (chronon3d.hpp,
#     runtime.hpp, internal.hpp).
#   * Core, scene, render_graph, etc. do not depend on it.
#
# Promotion to the public SDK requires all gates in docs/EXPRESSIONS_V2_PROMOTION.md.
# Do NOT add this subtree to the install() rules, the SDK export set, or to any
# chronon3d_* aggregate target until promotion is declared Done.

add_library(chronon3d_expressions_v2 STATIC
    src/expressions/v2/expression_value.cpp
    src/expressions/v2/lexer.cpp
    src/expressions/v2/type_checker.cpp
    src/expressions/v2/bytecode.cpp
    src/expressions/v2/compiler.cpp
    src/expressions/v2/vm.cpp
    src/expressions/v2/dependency_graph.cpp
)

# Headers live under ${CMAKE_CURRENT_SOURCE_DIR}/include/chronon3d_experimental/.
# expose only our own include/ to consumers (NOT ${CMAKE_SOURCE_DIR}/include).
# PUBLIC here would mean: any target that links chronon3d_expressions_v2 also
# gets this include path. For a quarantine, that's exactly what we DON'T want —
# the experimental include path should NOT propagate to any productive target
# that might one day accidentally link here. PRIVATE (with consumers using
# target_link_libraries PRIVATE on the experimental lib) keeps the include
# path local to the experimental subtree.
target_include_directories(chronon3d_expressions_v2
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(chronon3d_expressions_v2
    PUBLIC chronon3d
)
target_compile_features(chronon3d_expressions_v2 PUBLIC cxx_std_17)

# ── Experimental test suite ────────────────────────────────────────────────
# Only built when both CHRONON3D_BUILD_TESTS (parent flag) and
# CHRONON3D_BUILD_EXPERIMENTAL (this subtree's gate) are ON.
# The test executable includes a direct header-include canary that catches
# typos like TICKET-003's <chrono3d/...> in lexer.hpp by actually compiling
# each public header standalone.
if(CHRONON3D_BUILD_TESTS)
    include(${CMAKE_CURRENT_LIST_DIR}/tests/CMakeLists.txt)
endif()
