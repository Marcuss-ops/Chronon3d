# ── Determinism Tests ──
# Tests for pixel-perfect determinism, semantic comparison, and cache state effects.
# Requires Blend2D for software backend path rendering.

if(CHRONON3D_USE_BLEND2D)
chronon3d_add_test_suite(
    NAME chronon3d_deterministic_tests
    TIER INTEGRATION
    LINK_TARGETS chronon3d_sdk chronon3d_graph chronon3d_graph_pipeline chronon3d_backend_software chronon3d_scene
    SOURCES deterministic/test_deterministic.cpp
            deterministic/test_determinism_harness.cpp
            deterministic/gradient_determinism_tests.cpp
            # WP-6 PR 6.8 — baseline verde verificabile per docs/01-baseline-green.md
            # + docs/02-determinism.md.  Mitiga TICKET-007.q/r/s/t/u tramite
            # fresh-renderer-per-render + tbb::task_arena pin per render invece
            # di richiedere un fix globale del rot SIMD-path (ticket separato).
            deterministic/test_baseline_green.cpp
            # WP-6 PR 6.1 — TileGrid + DirtyTileMask determinism (pure
            # data structures, no TBB).  Proves the bit-pattern + iteration-
            # order invariants so any future change that introduces scheduler
            # state inside the tile data path fails this gate immediately
            # (the TICKET-007.q/r/s/t/u renderer-level non-determinism is a
            # separate ticket — see docs/FOLLOWUP_TICKETS.md).
            deterministic/test_tile_determinism.cpp
            # WP1 PR 1.4 — Scheduler-swap determinism (Sequential vs
            # TbbFixed/TbbAutomatic across required scenes).  Lives under
            # tests/render_graph/executor/ because the executor's scheduler
            # parameter (WP1 PR 1.0+1.1) is the test surface, but it reuses
            # the determinism harness helpers from this aggregate.
            render_graph/executor/test_scheduler_determinism.cpp
            # PR-A3 (Blocco A, Fase 2) — 15 minimal visual regression scenarios.
            # Extends the scheduler-level determinism fixture with scenario-
            # specific compositions (static, multiline, tracking, stroke, gradient,
            # shadow, glow, blur, typewriter, anim-glyph, anim-word, RTL, CJK,
            # emoji fallback, scale-extreme).  Sentinels captured on first clean
            # CI run per PR 6.8.5 Two-Phase Commit Strategy.  See
            # docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md §"Fase 2".
            deterministic/test_visual_regression_scenarios.cpp
            # P2-C — Determinism matrix: FNV-1a hash comparison across
            # 5 scene types × 6 conditions (same renderer, new renderer,
            # cache invalidation, thread count, 50× stress, sanity).
            deterministic/test_determinism_matrix.cpp
)
target_compile_definitions(chronon3d_deterministic_tests PRIVATE CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
endif()
