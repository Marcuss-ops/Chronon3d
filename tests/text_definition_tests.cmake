# tests/text_definition_tests.cmake
# ─────────────────────────────────────────────────────────────────────
# Canonical TextDefinition authoring and lowering tests.
#
# Highlights:
#   - Pure struct operations (no rendering / no Blend2D / no graphics
#     backend dependency).  The lowerer + round-trip exercises only the
#     existing TextDocumentBuilder (also pure-POD).
#   - Existing build dependency on `chronon3d_text_core` (covers both
#     text_definition.cpp + text_document_builder.cpp per
#     `src/text/CMakeLists.txt`).
#
# Registration helper: chronon3d_register_test_source() (cmake/
# Chronon3DTestSuite.cmake) so the §12 Python gate
# (tools/check_test_source_registration.py) tracks this file under
# the canonical test-source registry.
# ─────────────────────────────────────────────────────────────────────

chronon3d_add_test_suite(
    NAME chronon3d_text_definition_tests
    TIER UNIT
    LINK_TARGETS chronon3d_pipeline
    SOURCES text/test_text_definition_canonical.cpp
)
