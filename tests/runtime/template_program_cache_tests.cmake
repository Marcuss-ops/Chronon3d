# tests/runtime/template_program_cache_tests.cmake
# ════════════════════════════════════════════════════════════════════════════
# Fase H (TICKET-VIDEO-COMPILER-ARCH-V1) — TemplateProgramCache unit test
# registration. TIER=UNIT, UNCONDITIONAL per SDK-only build compatibility
# (mirrors the `fusion_pass_tests.cmake` + `text_definition_tests.cmake`
# UNCONDITIONAL pattern).  Exercises the LRU + pinned-residency semantics
# with hand-assembled CompiledTemplateProgram values — no GPU / font /
# compositor dependency.
# ════════════════════════════════════════════════════════════════════════════

chronon3d_add_test_suite(
    NAME chronon3d_template_program_cache_tests
    TIER UNIT
    SOURCES runtime/test_template_program_cache.cpp
)
