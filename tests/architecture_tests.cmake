find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_test(
    NAME chronon3d_architecture_includes_boundary
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/architecture/test_render_session_includes_boundary.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
set_tests_properties(chronon3d_architecture_includes_boundary PROPERTIES LABELS "architecture")

add_test(
    NAME chronon3d_architecture_render_graph_software_boundary
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/architecture/test_render_graph_excludes_software_private.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
set_tests_properties(chronon3d_architecture_render_graph_software_boundary PROPERTIES LABELS "architecture")

add_test(
    NAME chronon3d_text_definition_round_trip_parity
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/architecture/test_text_definition_round_trip_parity.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
set_tests_properties(chronon3d_text_definition_round_trip_parity PROPERTIES LABELS "architecture;text;parity")

add_test(
    NAME chronon3d_text_definition_round_trip_parity_py_compile
    COMMAND ${Python3_EXECUTABLE} -m py_compile ${CMAKE_CURRENT_SOURCE_DIR}/architecture/test_text_definition_round_trip_parity.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
set_tests_properties(chronon3d_text_definition_round_trip_parity_py_compile PROPERTIES LABELS "architecture;text;parity")

add_test(
    NAME chronon3d_gate11_backend_sanitization
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_backend_sanitization.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
set_tests_properties(chronon3d_gate11_backend_sanitization PROPERTIES LABELS "architecture;gate")

add_test(
    NAME chronon3d_gate11_backend_sanitization_py_compile
    COMMAND ${Python3_EXECUTABLE} -m py_compile ${CMAKE_SOURCE_DIR}/tools/check_backend_sanitization.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
set_tests_properties(chronon3d_gate11_backend_sanitization_py_compile PROPERTIES LABELS "architecture;gate")

add_test(
    NAME chronon3d_render_asset_architecture_guard
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_render_asset_architecture.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
set_tests_properties(chronon3d_render_asset_architecture_guard PROPERTIES
    LABELS "architecture;assets;render-job;gate")

add_test(
    NAME chronon3d_render_asset_architecture_guard_py_compile
    COMMAND ${Python3_EXECUTABLE} -m py_compile ${CMAKE_SOURCE_DIR}/tools/check_render_asset_architecture.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
set_tests_properties(chronon3d_render_asset_architecture_guard_py_compile PROPERTIES
    LABELS "architecture;assets;render-job;gate")

add_test(
    NAME chronon3d_authoring_public_header_closure
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_authoring_public_header_closure.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
set_tests_properties(chronon3d_authoring_public_header_closure PROPERTIES
    LABELS "architecture;sdk;authoring;gate")

add_test(
    NAME chronon3d_authoring_public_header_closure_py_compile
    COMMAND ${Python3_EXECUTABLE} -m py_compile ${CMAKE_SOURCE_DIR}/tools/check_authoring_public_header_closure.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
set_tests_properties(chronon3d_authoring_public_header_closure_py_compile PROPERTIES
    LABELS "architecture;sdk;authoring;gate")

add_test(
    NAME chronon3d_test_font_bootstrap_py_compile
    COMMAND ${Python3_EXECUTABLE} -m py_compile ${CMAKE_SOURCE_DIR}/tools/bootstrap_test_fonts.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
set_tests_properties(chronon3d_test_font_bootstrap_py_compile PROPERTIES
    LABELS "architecture;assets;fonts;gate")

add_test(
    NAME chronon3d_asset_consumer_shell_syntax
    COMMAND bash -n ${CMAKE_SOURCE_DIR}/tools/sdk/run_asset_authoring_consumer.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
set_tests_properties(chronon3d_asset_consumer_shell_syntax PROPERTIES
    LABELS "architecture;assets;sdk;gate")
