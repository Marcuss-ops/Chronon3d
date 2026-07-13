# tests/simd/simd_parity_blend_tests.cmake
# ════════════════════════════════════════════════════════════════════════════
# F5.2 first-kernel (TICKET-SIMD-VECTORIZE-KERNEL-SET-V1) —
# scalar-vs-AVX2 parity test registration for the premultiplied-alpha
# "SRC_OVER" blend (the F5.2 first kernel).
#
# Pure API contract test (no Blend2D, no GPU, no FontEngine, no
# framebuffer I/O).  TIER=UNIT and UNCONDITIONAL registration per the
# `tests/CMakeLists.txt` orchestrator pattern (mirrors
# `text_definition_tests.cmake` + `safe_area_placement_tests.cmake`):
#   - the test exercises the SIMD registry ABI surface (the
#     `PixelKernelSet` struct + `resolve_pixel_kernels` resolver +
#     the per-ISA `kScalarSet`/`kAvx2Set` statics) without any
#     rendering backend, font engine, or compositor dependency;
#   - the include must register on SDK-only builds where
#     `CHRONON3D_USE_BLEND2D` / `CHRONON3D_ENABLE_TEXT` are OFF
#     (canonical SDK-only-build compatibility per AGENTS.md v0.1
#     §regole "no espansione API non necessaria" — the test
#     validates the ABI surface itself, not the rendering path).
# ════════════════════════════════════════════════════════════════════════════

chronon3d_add_test_suite(
    NAME chronon3d_simd_parity_blend_tests
    TIER UNIT
    SOURCES simd/test_simd_parity_blend.cpp
)
