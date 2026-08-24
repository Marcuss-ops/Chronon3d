if(NOT CHRONON3D_ENABLE_IPC OR NOT TARGET chronon3d_ipc)
    return()
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_test(
    NAME chronon3d_ipc_schema_documents_tests
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_ipc_schema_documents.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
set_tests_properties(chronon3d_ipc_schema_documents_tests PROPERTIES
    LABELS "ipc;schema;contract")

chronon3d_add_test_suite(
    NAME chronon3d_contract_validator_registry_tests
    TIER INTEGRATION
    LINK_TARGETS chronon3d_ipc chronon3d_pipeline chronon3d_backend_software
    SOURCES ipc/test_contract_validator_registry.cpp
    LABELS ipc\;schema\;contract)

chronon3d_add_test_suite(
    NAME chronon3d_contract_validator_e2e_tests
    TIER INTEGRATION
    LINK_TARGETS chronon3d_ipc chronon3d_pipeline chronon3d_backend_software
    SOURCES ipc/test_contract_validator_e2e.cpp
    LABELS ipc\;schema\;contract\;e2e)
