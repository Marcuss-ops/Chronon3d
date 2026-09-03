#!/usr/bin/env bash
# tools/gates/manifest.sh — canonical gate manifest
# Static architecture invariants have one authority: check_architecture.py
# reading tools/architecture_rules.toml and its declared registry fragments.

DEVELOPER_GATES=(
    check_test_hygiene.sh
    check_frame_value_convention.sh
    check_no_source_conflict_markers.sh
    check_no_changelog_conflict_markers.sh
    check_doc_sha_dedup.sh
    check_commit_subject_length.sh
    check_architecture.py
)

CI_PHASES=(
    build_fast
    unit_tests
)

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
