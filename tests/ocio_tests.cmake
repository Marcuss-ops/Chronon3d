if(NOT CHRONON3D_BUILD_TESTS OR NOT CHRONON3D_ENABLE_OCIO)
    return()
endif()

chronon3d_add_test_suite(
    NAME chronon3d_ocio_tests
    TIER UNIT
    LINK_TARGETS
        chronon3d_core_impl
        OpenColorIO::OpenColorIO
    SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/core/math/test_ocio_color_transform.cpp
)
