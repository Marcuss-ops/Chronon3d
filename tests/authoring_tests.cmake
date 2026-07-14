# ── Authoring DSL Tests ─────────────────────────────────────────────────
# Per-area early-return gate (TICKET-CMAKE-TEST-MANIFEST-UNIFICATION).
# Authoring tests require the text subsystem (glyph_selector, evaluate_selector, etc.)
# so they are gated by the same Blend2D+Text guard as the body of the
# pre-refactor orchestrator's nested `if(CHRONON3D_USE_BLEND2D AND CHRONON3D_ENABLE_TEXT)`.
if(NOT (CHRONON3D_USE_BLEND2D AND CHRONON3D_ENABLE_TEXT))
    return()
endif()
# Equivalence tests for the fluent authoring façade
# (chronon3d::authoring::Animator / Selector). Pure header-only logic,
# no render backend / asset I/O — fits in the fast-tests bucket.

chronon3d_add_test_suite(
    NAME chronon3d_authoring_tests
    TIER UNIT
    LINK_TARGETS chronon3d_sdk chronon3d_sdk_impl chronon3d_pipeline
    # Audit §10 — process-wide asset root ripout.  Four pure
    # header-only tests for the thin `authoring::asset(...)` family
    # + ImageRef/FontRef overloads on `Layer::image`/`Text::font`.
    SOURCES authoring/test_animator_dsl.cpp
            authoring/test_asset_api.cpp
)
# Same guard as core_tests.cmake: authoring tests share test_main.cpp
# which conditionally links against content/extension/text symbols.
if(CHRONON3D_ENABLE_TEXT AND CHRONON3D_USE_BLEND2D AND TARGET chronon3d_backend_text)
    target_link_libraries(chronon3d_authoring_tests PRIVATE chronon3d_backend_text)
endif()
# Keep the test-only authoring::testing release accessor visible (the
# `friend struct testing::AnimatorTestAccess` declaration in the
# authoring headers is gated on the test build so it does not leak into
# production SDK).
target_compile_definitions(chronon3d_authoring_tests PRIVATE CHRONON3D_BUILD_TESTS)
