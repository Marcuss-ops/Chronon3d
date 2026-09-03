# Runtime parallelism regression: verifies the executor actually engages
# multiple TBB workers when parallel regions are available.
if(NOT (CHRONON3D_USE_BLEND2D AND CHRONON3D_ENABLE_TEXT))
    return()
endif()

chronon3d_add_test_suite(
    NAME chronon3d_tbb_workers_test
    TIER INTEGRATION
    LINK_TARGETS chronon3d_sdk chronon3d_backend_software chronon3d_scene
    SOURCES render_graph/executor/test_tbb_workers_parallelism.cpp
)
target_compile_definitions(
    chronon3d_tbb_workers_test
    PRIVATE CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
