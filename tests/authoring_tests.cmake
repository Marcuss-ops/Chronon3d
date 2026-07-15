# ── Authoring DSL Tests ─────────────────────────────────────────────────
if(NOT (CHRONON3D_USE_BLEND2D AND CHRONON3D_ENABLE_TEXT))
    return()
endif()

chronon3d_add_test_suite(
    NAME chronon3d_authoring_tests
    TIER UNIT
    LINK_TARGETS chronon3d_sdk chronon3d_sdk_impl chronon3d_pipeline
    SOURCES authoring/test_selector_animator_dsl.cpp
            authoring/test_material_dsl.cpp
            authoring/test_registry_dsl.cpp
            authoring/test_text_layer_facade.cpp
            authoring/test_text_registry_resolution.cpp
            authoring/test_scene_composition_facade.cpp
            authoring/test_asset_api.cpp
            authoring/test_scene_sequence.cpp
            authoring/test_scene_forwarders.cpp
)

if(CHRONON3D_ENABLE_TEXT AND CHRONON3D_USE_BLEND2D AND TARGET chronon3d_backend_text)
    target_link_libraries(chronon3d_authoring_tests PRIVATE chronon3d_backend_text)
endif()
