# ── Animation Catalog Tests ──
# Per-area early-return gate (TICKET-CMAKE-TEST-MANIFEST-UNIFICATION).
if(NOT (CHRONON3D_USE_BLEND2D AND CHRONON3D_ENABLE_TEXT))
    return()
endif()

chronon3d_add_test_suite(
    NAME chronon3d_animation_tests
    TIER UNIT
    LINK_TARGETS chronon3d_sdk chronon3d_sdk_impl chronon3d_pipeline
    SOURCES
        animations/test_background_catalog.cpp
        text/test_video_animation_curves.cpp
)
# WHOLE_ARCHIVE removed: content targets are now small and explicitly registered
if(CHRONON3D_BUILD_CONTENT)
    target_link_libraries(chronon3d_animation_tests PRIVATE chronon3d_content)
    target_compile_definitions(chronon3d_animation_tests PRIVATE
        CHRONON3D_HAS_CONTENT_MINIMALIST
        CHRONON3D_HAS_CONTENT_BACKGROUNDS
    )
endif()
