find_package(Python3 COMPONENTS Interpreter REQUIRED)

# All static architecture invariants are executed through the canonical
# declarative registry. Do not register standalone static checker scripts here.
add_test(
    NAME chronon3d_architecture_registry
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_architecture.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
set_tests_properties(chronon3d_architecture_registry PROPERTIES
    LABELS "architecture;gate"
)

# Syntax-only SDK consumer smoke check is procedural rather than a static
# architecture rule, so it remains an independent CTest.
add_test(
    NAME chronon3d_asset_consumer_shell_syntax
    COMMAND bash -n ${CMAKE_SOURCE_DIR}/tools/sdk/run_asset_authoring_consumer.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
set_tests_properties(chronon3d_asset_consumer_shell_syntax PROPERTIES
    LABELS "architecture;assets;sdk;gate"
)
