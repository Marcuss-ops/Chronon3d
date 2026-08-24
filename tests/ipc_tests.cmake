if(NOT CHRONON3D_BUILD_TESTS)
    return()
endif()
if(NOT CHRONON3D_ENABLE_IPC OR NOT TARGET chronon3d_ipc)
    return()
endif()

find_package(flatbuffers CONFIG REQUIRED)
find_package(nlohmann_json_schema_validator CONFIG REQUIRED)

chronon3d_add_test_suite(
    NAME chronon3d_ipc_tests
    TIER UNIT
    SOURCES ipc/test_ipc_codec.cpp ipc/test_shared_memory_transport.cpp $<TARGET_OBJECTS:chronon3d_ipc>
    LINK_TARGETS chronon3d_pipeline flatbuffers::flatbuffers
                 nlohmann_json_schema_validator::validator
)

target_include_directories(chronon3d_ipc_tests
    PRIVATE "${CHRONON3D_IPC_GENERATED_DIR}"
            "${CMAKE_SOURCE_DIR}/src/ipc"
)
