if(NOT CHRONON3D_BUILD_TESTS)
    return()
endif()

find_package(flatbuffers CONFIG REQUIRED)

chronon3d_add_test_suite(
    NAME chronon3d_ipc_tests
    TIER UNIT
    SOURCES ipc/test_ipc_codec.cpp $<TARGET_OBJECTS:chronon3d_ipc>
    LINK_TARGETS chronon3d_pipeline flatbuffers::flatbuffers
)

target_include_directories(chronon3d_ipc_tests
    PRIVATE "${CHRONON3D_IPC_GENERATED_DIR}"
            "${CMAKE_SOURCE_DIR}/src/ipc"
)
