#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# tools/gates/manifest.sh — canonical gate manifest
#
# Defines the gate lists used by:
#   - tools/run_developer_gates.sh
#   - tools/wrap_push.sh
#   - CI pipelines
#
# Source this file; do not execute it directly. All paths are relative to the
# repository root `tools/` directory.
#
# Gate tiers:
#   DEVELOPER_GATES — fast, local-only checks safe on every push.
#   CI_GATES        — DEVELOPER_GATES plus build + unit-test validation.
#   WBH_GATES       — CI_GATES plus build-host certification gates.
# ═══════════════════════════════════════════════════════════════════════════

# Developer gates: fast, local-only, safe on any push.
DEVELOPER_GATES=(
    check_test_hygiene.sh
    check_test_suite_registration.sh
    check_frame_value_convention.sh
    check_no_changelog_conflict_markers.sh
    check_doc_sha_dedup.sh
    check_commit_subject_length.sh
    check_unique_cmake_source_ownership.py
    check_no_process_wide_caches.sh
    check_no_runtime_image_decode.sh
    check_no_hidden_render_scratch.sh
    check_no_dead_shape_type_text.sh
    check_no_preset_catalog_magic_statics.sh
    check_no_software_effect_dispatch_switch.sh
    check_effect_subsystem_ownership.sh
    check_effect_processor_coverage.py
    check_no_text_material_external_effects.sh
    check_architecture_boundaries.sh
)

# The common performance contract is report-driven and opt-in. It is not
# included in push-time developer gates because those gates intentionally do
# not require benchmark artifacts.

# CI-only phases (not executable gate scripts; handled by the CI driver).
CI_PHASES=(
    build_fast
    unit_tests
)

# WBH-only gates that still represent current engine/SDK contracts. Historical
# showcase, product-metric, batch-manual-touch and external ffprobe gates were
# removed together with their retired fixtures/configurations.
WBH_ONLY_GATES=(
    verify_cli_render_surface_linux.sh
    verify_sdk_consumer_functional_linux.sh
    check_determinism.sh
    check_determinism_matrix.sh
)

CI_GATES=(
    "${DEVELOPER_GATES[@]}"
    "${CI_PHASES[@]}"
)

WBH_GATES=(
    "${CI_GATES[@]}"
    "${WBH_ONLY_GATES[@]}"
)
