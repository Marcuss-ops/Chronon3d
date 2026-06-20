# ==============================================================================
# src/expressions/v2/CMakeLists.txt — chronon3d_expressions_v2
# ==============================================================================
#
# Path B expression engine. Lives on `feature/expressions-v2` branch until
# merge post-freeze (main stays on Path A). The `math` and `core` libraries
# are PUBLIC dependencies for `Vec2/Vec3/Vec4`/frame types used in the std::variant.

add_library(chronon3d_expressions_v2 STATIC
    expression_value.cpp
    lexer.cpp
    type_checker.cpp
    bytecode.cpp
    compiler.cpp
    vm.cpp
    dependency_graph.cpp
)

# chronon3d INTERFACE target exposes `${CMAKE_SOURCE_DIR}/include` to its consumers
# via PUBLIC link — but for this lib's OWN build we must re-declare the dir so
# that `#include <chronon3d/expressions/v2/expression_value.hpp>` resolves to
# `<root>/include/chronon3d/expressions/v2/expression_value.hpp`.
# TICKET-004: previously PUBLIC (over-exposing ${CMAKE_SOURCE_DIR}/include
# to all transitive consumers). Changed to PRIVATE — chronon3d is linked
# PUBLIC below and already exports this include dir transitively.
target_include_directories(chronon3d_expressions_v2
    PRIVATE ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(chronon3d_expressions_v2
    PUBLIC chronon3d
)
target_compile_features(chronon3d_expressions_v2 PUBLIC cxx_std_17)
