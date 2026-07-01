find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_test(
    NAME chronon3d_architecture_includes_boundary
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/architecture/test_render_session_includes_boundary.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
set_tests_properties(chronon3d_architecture_includes_boundary PROPERTIES LABELS "architecture")

# P0-5 — register the render-graph -> software-include_private boundary
# guard.  Pattern precedent: chronon3d_architecture_includes_boundary above.
# See tests/architecture/test_render_graph_excludes_software_private.py
# for the rule semantics.
add_test(
    NAME chronon3d_architecture_render_graph_software_boundary
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/architecture/test_render_graph_excludes_software_private.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
set_tests_properties(chronon3d_architecture_render_graph_software_boundary PROPERTIES LABELS "architecture")
