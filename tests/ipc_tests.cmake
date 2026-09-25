if(NOT CHRONON3D_BUILD_TESTS)
    return()
endif()
if(NOT CHRONON3D_ENABLE_IPC OR NOT TARGET chronon3d_ipc)
    return()
endif()

find_package(flatbuffers CONFIG REQUIRED)
find_package(nlohmann_json_schema_validator CONFIG REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

chronon3d_add_test_suite(
    NAME chronon3d_ipc_tests
    TIER UNIT
    SOURCES ipc/test_ipc_codec.cpp ipc/test_daemon_job_protocol.cpp
            ipc/test_shared_memory_transport.cpp ipc/test_unix_socket_transport.cpp
            ${CMAKE_SOURCE_DIR}/src/daemon/daemon_runtime.cpp
            $<TARGET_OBJECTS:chronon3d_ipc>
    LINK_TARGETS chronon3d_pipeline chronon3d_backend_software
                 flatbuffers::flatbuffers
                 nlohmann_json_schema_validator::validator
)

target_include_directories(chronon3d_ipc_tests
    PRIVATE "${CHRONON3D_IPC_GENERATED_DIR}"
            "${CMAKE_SOURCE_DIR}/src/ipc"
            "${CMAKE_SOURCE_DIR}/src/daemon"
)

# ── IPC schema and contract tests ──
add_test(
    NAME chronon3d_ipc_schema_documents_tests
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_ipc_schema_documents.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
set_tests_properties(chronon3d_ipc_schema_documents_tests PROPERTIES
    LABELS "ipc;schema;contract")

# Gate: sdk-dist/schemas must be generated, not hand-copied; it must be
# byte-identical to schemas/json/ when present (see tools/check_schema_sdk_parity.py).
add_test(
    NAME chronon3d_sdk_schema_parity_tests
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_schema_sdk_parity.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
set_tests_properties(chronon3d_sdk_schema_parity_tests PROPERTIES
    LABELS "schema;contract;parity")

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
