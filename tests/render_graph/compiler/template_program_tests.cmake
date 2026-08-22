# tests/render_graph/compiler/template_program_tests.cmake
# ════════════════════════════════════════════════════════════════════════════
# Fase A (TICKET-VIDEO-COMPILER-ARCH-V1) — CompiledTemplateProgram ABI +
# derivation unit test registration. TIER=UNIT, UNCONDITIONAL per SDK-only
# build compatibility (mirrors the `fusion_pass_tests.cmake` +
# `text_definition_tests.cmake` + `safe_area_placement_tests.cmake`
# UNCONDITIONAL pattern). The test exercises only the ABI surface
# (ProgramFingerprint / ParameterSchema / ResourceManifest / StaticBakeRegion /
# CompiledTemplateProgram + compile_template_program) with a hand-assembled
# CompiledFrameGraph — no rendering backend, font engine, or compositor
# dependency.
# ════════════════════════════════════════════════════════════════════════════

chronon3d_add_test_suite(
    NAME chronon3d_template_program_tests
    TIER UNIT
    SOURCES render_graph/compiler/test_template_program.cpp
)
