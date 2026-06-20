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

target_include_directories(chronon3d_expressions_v2
    PUBLIC ${CMAKE_SOURCE_DIR}
)
target_link_libraries(chronon3d_expressions_v2
    PUBLIC chronon3d
)
target_compile_features(chronon3d_expressions_v2 PUBLIC cxx_std_17)
