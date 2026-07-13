# tests/render_graph/compiler/fusion_pass_tests.cmake
# ════════════════════════════════════════════════════════════════════════════
# F3.1 (TICKET-FUSION-PASS-COMPILER-V1) — FusedPixelProgram ABI unit test
# registration. TIER=UNIT, UNCONDITIONAL per SDK-only build compatibility
# (mirrors the `text_definition_tests.cmake` + `safe_area_placement_tests.cmake`
# UNCONDITIONAL pattern). The test exercises only the F3.1 ABI surface
# (PixelOperation / PixelKernel / FusedColorOpacityBlendGuard /
# FusedPixelProgram / FusionStats) without any rendering backend, font
# engine, or compositor dependency.
# ════════════════════════════════════════════════════════════════════════════

chronon3d_add_test_suite(
    NAME chronon3d_fusion_pass_tests
    TIER UNIT
    SOURCES render_graph/compiler/test_fusion_pass.cpp
)
