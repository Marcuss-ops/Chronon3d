# ── IO Tests ──

chronon3d_add_test_suite(
    NAME chronon3d_io_tests
    TIER SDK
    LINK_TARGETS chronon3d_sdk chronon3d_sdk_impl chronon3d_pipeline chronon3d_backend_image
    SOURCES io/test_image_writer.cpp
            io/test_png_validity.cpp
            # TICKET-RENDER-PIPELINE-INTEGRITY layer 2 — save_png throws
            # std::runtime_error on a NaN/Inf pixel (was silent zero-fill).
            io/test_image_writer_throw.cpp
)
if(CHRONON3D_ENABLE_EXR)
    target_sources(chronon3d_io_tests PRIVATE io/test_exr_writer.cpp)
endif()
