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

# Gate 11 — Backend sanitization checks (legacy files, legacy references,
# debug smoke signals, test smoke signals).  Invokes the Python script via
# Python3_EXECUTABLE (not bash) so the import statements execute correctly.
add_test(
    NAME chronon3d_gate11_backend_sanitization
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_backend_sanitization.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
set_tests_properties(chronon3d_gate11_backend_sanitization PROPERTIES LABELS "architecture;gate")

# py_compile check — verifies check_backend_sanitization.py has no syntax
# errors.  This catches Python syntax regressions at configure/test time
# without requiring the full sanitization logic to run.
add_test(
    NAME chronon3d_gate11_backend_sanitization_py_compile
    COMMAND ${Python3_EXECUTABLE} -m py_compile ${CMAKE_SOURCE_DIR}/tools/check_backend_sanitization.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
set_tests_properties(chronon3d_gate11_backend_sanitization_py_compile PROPERTIES LABELS "architecture;gate")
