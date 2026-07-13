# tests/render_graph/pipeline/glow_fullframe_audit_tests.cmake
# ─────────────────────────────────────────────────────────────────────────
# F3.2 (TICKET-GLOW-FULLFRAME-AUDIT-V1) — B03 CinematicGlow1080p 4-gate
# synthetic ledger test.  Mirrors the F3.1 + F5.2 registration pattern:
# TIER=UNIT, UNCONDITIONAL registration (no Blend2D / text / video /
# GPU dependency — pure stdlib + doctest contract lock).
#
# Test surface:
#   - 4 B03 CinematicGlow1080p gates (avoidable copies + redundant clears
#     + dirty rect respected + no duplicate format conversions)
#   - Per-frame rate derivation contract (matches graph_total_ms precedent)
#   - Atomic counter type contract (CHRONON_COUNTERS_GRAPH conformance)
#   - TLS merge monotonicity (anti-false-sharing discipline)
#   - Cat-3 forbidden-include self-check
#
# ABI coverage:
#   - include/chronon3d/core/profiling/render_counter_types.hpp
#     (full_frame_passes + full_frame_copies + dirty_full_fallbacks —
#      pre-existing)
#   - BenchmarkCountersSnapshot::*_per_frame derived fields
#     (full_frame_passes_per_frame + full_frame_copies_per_frame)
#
# Per ADR-018, this .cmake is also listed in CMAKE_CONFIGURE_DEPENDS at
# the top of tests/CMakeLists.txt.
# ─────────────────────────────────────────────────────────────────────────

chronon3d_add_test_suite(
    NAME chronon3d_glow_fullframe_audit_tests
    TIER UNIT
    SOURCES
        render_graph/pipeline/test_glow_fullframe_audit.cpp
)
