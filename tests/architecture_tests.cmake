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

# TICKET-SIMPLICITY-PIPELINE-PARITY — Python source-level audit verifying
# that TextSpec ↔ TextDefinition round-trip covers all 30 sub-fields
# bidirectionally.  PASS = all fields mapped, guaranteeing 0px render diff.
add_test(
    NAME chronon3d_text_definition_round_trip_parity
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/architecture/test_text_definition_round_trip_parity.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
set_tests_properties(chronon3d_text_definition_round_trip_parity PROPERTIES LABELS "architecture;text;parity")

# py_compile check — verifies test_text_definition_round_trip_parity.py
# has no syntax errors.
add_test(
    NAME chronon3d_text_definition_round_trip_parity_py_compile
    COMMAND ${Python3_EXECUTABLE} -m py_compile ${CMAKE_CURRENT_SOURCE_DIR}/architecture/test_text_definition_round_trip_parity.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
set_tests_properties(chronon3d_text_definition_round_trip_parity_py_compile PROPERTIES LABELS "architecture;text;parity") (legacy files, legacy references,
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
