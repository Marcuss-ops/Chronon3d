# ── Chronon3D IPC module (ADR-024) ────────────────────────────────────────
# FlatBuffers schema → generated C++ → OBJECT library
#
# Schema compilation: flatc --cpp schemas/flatbuffers/chronon_ipc.fbs → chronon_ipc_generated.h

find_program(CHRONON3D_FLATC flatc REQUIRED)
find_package(flatbuffers CONFIG REQUIRED)

set(CHRONON3D_IPC_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(CHRONON3D_IPC_SCHEMA "${CMAKE_SOURCE_DIR}/schemas/flatbuffers/chronon_ipc.fbs")
set(CHRONON3D_IPC_GENERATED_HEADER "${CHRONON3D_IPC_GENERATED_DIR}/chronon_ipc_generated.h")

set(CHRONON3D_COMPOSITION_SCHEMA_FILE
    ${CMAKE_SOURCE_DIR}/schemas/json/chronon.composition.v1.schema.json)
set(CHRONON3D_RENDER_PLAN_SCHEMA_FILE
    ${CMAKE_SOURCE_DIR}/schemas/json/chronon.render-plan.v2.schema.json)
set(CHRONON3D_RENDER_SETTINGS_SCHEMA_FILE
    ${CMAKE_SOURCE_DIR}/schemas/json/chronon.render-settings.v1.schema.json)
file(READ "${CHRONON3D_COMPOSITION_SCHEMA_FILE}"
     CHRONON3D_COMPOSITION_SCHEMA_JSON)
file(READ "${CHRONON3D_RENDER_PLAN_SCHEMA_FILE}"
     CHRONON3D_RENDER_PLAN_SCHEMA_JSON)
file(READ "${CHRONON3D_RENDER_SETTINGS_SCHEMA_FILE}"
     CHRONON3D_RENDER_SETTINGS_SCHEMA_JSON)
set(CHRONON3D_IPC_SCHEMA_HEADER
    "${CHRONON3D_IPC_GENERATED_DIR}/contract_validator_schemas.hpp")
set(CHRONON3D_IPC_SCHEMA_FILES
    ${CHRONON3D_COMPOSITION_SCHEMA_FILE}
    ${CHRONON3D_RENDER_PLAN_SCHEMA_FILE}
    ${CHRONON3D_RENDER_SETTINGS_SCHEMA_FILE})
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/contract_validator_schemas.hpp.in
    ${CHRONON3D_IPC_SCHEMA_HEADER}
    @ONLY
)

add_custom_command(
    OUTPUT "${CHRONON3D_IPC_GENERATED_HEADER}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CHRONON3D_IPC_GENERATED_DIR}"
    COMMAND "${CHRONON3D_FLATC}"
        --cpp
        -o "${CHRONON3D_IPC_GENERATED_DIR}"
        "${CHRONON3D_IPC_SCHEMA}"
    DEPENDS "${CHRONON3D_IPC_SCHEMA}" ${CHRONON3D_IPC_SCHEMA_FILES}
    COMMENT "Compiling FlatBuffers schema: chronon_ipc.fbs → C++"
    VERBATIM
)

add_library(chronon3d_ipc OBJECT
    ipc_codec.cpp
    composition_session.cpp
    contract_validator_registry.cpp
    transport/memfd_shared_memory_transport.cpp
    "${CHRONON3D_IPC_GENERATED_HEADER}"
    "${CHRONON3D_IPC_SCHEMA_HEADER}"
)
set_target_properties(chronon3d_ipc PROPERTIES UNITY_BUILD OFF)
find_package(nlohmann_json_schema_validator CONFIG REQUIRED)

target_link_libraries(chronon3d_ipc PUBLIC
    chronon3d
    flatbuffers::flatbuffers
    nlohmann_json_schema_validator::validator
)
target_include_directories(chronon3d_ipc
    PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
           $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
           $<BUILD_INTERFACE:${CHRONON3D_IPC_GENERATED_DIR}>
    PRIVATE ${CMAKE_SOURCE_DIR}
)

# Publish to the canonical registry
set(CHRONON3D_IPC_GENERATED_DIR "${CHRONON3D_IPC_GENERATED_DIR}" CACHE INTERNAL
    "IPC FlatBuffers generated header directory")
set(CHRONON3D_IPC_SCHEMA_HEADER "${CHRONON3D_IPC_SCHEMA_HEADER}" CACHE INTERNAL
    "Embedded IPC JSON Schema header directory")

if(CHRONON3D_BUILD_FUZZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "CHRONON3D_BUILD_FUZZERS requires a Clang compiler with libFuzzer")
    endif()
    add_executable(chronon3d_ipc_fuzzer
        fuzz/ipc_codec_fuzzer.cpp
        $<TARGET_OBJECTS:chronon3d_ipc>
    )
    target_include_directories(chronon3d_ipc_fuzzer PRIVATE
        "${CHRONON3D_IPC_GENERATED_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}"
    )
    target_link_libraries(chronon3d_ipc_fuzzer PRIVATE flatbuffers::flatbuffers)
    target_compile_options(chronon3d_ipc_fuzzer PRIVATE -fsanitize=fuzzer,address,undefined)
    target_link_options(chronon3d_ipc_fuzzer PRIVATE -fsanitize=fuzzer,address,undefined)
endif()
