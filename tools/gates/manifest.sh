#!/usr/bin/env bash
# tools/gates/manifest.sh — canonical gate manifest
# Static architecture invariants have one authority: check_architecture.py
# reading tools/architecture_rules.toml and its declared registry fragments.
#
# 2026-09-26 slim: pre-push è 6 gate <90s. Tutto il resto è CI / full.
# - DEVELOPER_GATES       → fail-closed su ogni `git push` (wrap_push + pre-push hook)
# - PRE_PUSH_SLOW_GATES   → solo con CHRONON3D_PRE_PUSH_FULL=1 o in CI
# - FULL_DEVELOPER_GATES  → DEVELOPER + SLOW (certificazione completa locale)
# - CI_GATES / WBH_GATES  → CI fa girare DEVELOPER+SLOW+build phases

DEVELOPER_GATES=(
    check_conflict_markers.sh
    check_commit_subject_length.sh
    check_no_sqlite.sh
    check_doc_sha_dedup.sh
    check_frame_value_convention.sh
    check_architecture.py
)

# Lenti, build o meta-gate: non su ogni push.  CI li copre.
# - check_test_hygiene* / architecture_gate_selftest / node_memory_metrics_selftest = meta
# - check_cli_tests / verify_registry_render_graph_cache = build (30s-10min)
# - check_steady_state_allocations = warn-only su baseline storica
PRE_PUSH_SLOW_GATES=(
    check_test_hygiene.sh
    check_test_hygiene_registry_selftest.sh
    architecture_gate_selftest.sh
    node_memory_metrics_selftest.sh
    check_steady_state_allocations.sh
    check_cli_tests.sh
    verify_registry_render_graph_cache.sh
)

CI_PHASES=(
    build_fast
    unit_tests
)

WBH_ONLY_GATES=(
    verify_cli_render_surface_linux.sh
    verify_sdk_consumer_functional_linux.sh
    verify_text_tile_gpu_canary.sh
    verify_text_typewriter_golden.sh
    check_determinism.sh
    check_determinism_matrix.sh
)

FULL_DEVELOPER_GATES=(
    "${DEVELOPER_GATES[@]}"
    "${PRE_PUSH_SLOW_GATES[@]}"
)

CI_GATES=(
    "${DEVELOPER_GATES[@]}"
    "${PRE_PUSH_SLOW_GATES[@]}"
    "${CI_PHASES[@]}"
)

WBH_GATES=(
    "${CI_GATES[@]}"
    "${WBH_ONLY_GATES[@]}"
)
