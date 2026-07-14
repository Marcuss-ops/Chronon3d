# ── IO Tests ──
# Per-area early-return gate (TICKET-CMAKE-TEST-MANIFEST-UNIFICATION).
# IO tests use the chronon3d_backend_image OBJECT library which technically
# only requires the image backend (not all of Blend2D), but for the
# pre-refactor orchestrator's intent we preserve the Blend2D+Text gate
# so any SDK-only build that disables Blend2D is unaffected.
if(NOT (CHRONON3D_USE_BLEND2D AND CHRONON3D_ENABLE_TEXT))
    return()
endif()

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
