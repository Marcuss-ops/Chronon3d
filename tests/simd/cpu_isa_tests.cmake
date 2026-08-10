# tests/simd/cpu_isa_tests.cmake
# ════════════════════════════════════════════════════════════════════════════
# Registration for the canonical CPU ISA detection API test
# (`simd/test_cpu_isa.cpp` — cpu_isa_name/parse_cpu_isa/supports/
# detect_cpu_capabilities, implemented in src/backends/software/simd/
# cpu_isa.cpp).
#
# Pure API contract test (no rendering backend). TIER=UNIT and
# UNCONDITIONAL registration per the `tests/CMakeLists.txt` orchestrator
# pattern (mirrors `simd/simd_parity_blend_tests.cmake`): the API is
# always compiled into `chronon3d_backend_software` (aggregated into the
# SDK impl target), so the test must register on every build.
# ════════════════════════════════════════════════════════════════════════════

chronon3d_add_test_suite(
    NAME chronon3d_cpu_isa_tests
    TIER UNIT
    SOURCES simd/test_cpu_isa.cpp
)
