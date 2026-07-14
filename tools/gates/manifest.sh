#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# tools/gates/manifest.sh — canonical gate manifest
#
# Defines the gate lists used by:
#   - tools/run_developer_gates.sh
#   - tools/wrap_push.sh
#   - CI pipelines
#
# Source this file; do not execute it directly.  All paths are relative to
# the repository root `tools/` directory.
#
# Gate tiers:
#   DEVELOPER_GATES — fast, local-only checks safe on every push
#                     (no build artifacts required).
#   CI_GATES        — DEVELOPER_GATES plus build + unit-test validation.
#   WBH_GATES       — CI_GATES plus product-level certification gates that
#                     require a working build host (MP4, glow, batch, SDK).
# ═══════════════════════════════════════════════════════════════════════════

# Developer gates: fast, local-only, safe on any push.
# These gates do not require build artifacts (MP4, glow output, etc.).
DEVELOPER_GATES=(
    check_test_hygiene.sh
    check_test_suite_registration.sh
    check_frame_value_convention.sh
    check_no_changelog_conflict_markers.sh
    check_text_golden_sources_aligned.sh
    check_doc_sha_dedup.sh
    check_commit_subject_length.sh
    check_push_divergence_window.sh
    check_architecture_boundaries.sh
)

# Post-push gates: run only after a successful push (not in the pre-push
# developer chain).  These gates verify that the chore actually landed on
# the remote and was not silently rebased out by concurrent-agent churn.
POST_PUSH_GATES=(
    check_post_push_consistency.sh
)

# CI-only phases (not executable gate scripts; handled by the CI driver).
CI_PHASES=(
    build_fast
    unit_tests
)

# WBH-only gates: product-level certification gates that require build
# artifacts and a working build host.  Kept separate so the push wrapper
# can run developer gates once and then append only these extra gates.
WBH_ONLY_GATES=(
    check_video_completeness.sh
    check_fix_cronograph.sh
    check_manual_touches_per_video.sh
    check_batch_100_videos.sh
    verify_sdk_consumer_functional_linux.sh
    check_glow_certification.sh
    check_determinism.sh
    check_determinism_matrix.sh
)

# CI gates: developer gates plus CI-specific build/test phases.
# Note: CI_PHASES are abstract driver steps, not executable scripts.
CI_GATES=(
    "${DEVELOPER_GATES[@]}"
    "${CI_PHASES[@]}"
)

# WBH gates: full certification chain (CI gates + WBH-only gates).
WBH_GATES=(
    "${CI_GATES[@]}"
    "${WBH_ONLY_GATES[@]}"
)
