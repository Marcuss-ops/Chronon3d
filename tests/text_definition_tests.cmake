# tests/text_definition_tests.cmake
# ─────────────────────────────────────────────────────────────────────
# TICKET-SIMPLICITY-TEXTDEFINITION §3 — Lock tests for the canonical
# TextDefinition adapter chain (from_text_spec / from_text_run_spec /
# from_text_definition + compile_to_canonical).
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
    LINK_TARGETS chronon3d_text_core
    SOURCES text/test_text_definition.cpp
)
