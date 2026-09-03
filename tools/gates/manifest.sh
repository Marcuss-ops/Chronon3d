#!/usr/bin/env bash
# shellcheck shell=bash

# Canonical gate manifest.
#
# This file is the single source of truth for:
# - fast developer/pre-push gates;
# - WBH hardware-sensitive gates.
#
# The manifest must stay declarative: no execution side effects.

CHRONON3D_DEVELOPER_GATES=(
    "check_test_hygiene.sh"
    "check_test_suite_registration.sh"
    "check_frame_value_convention.sh"
    "check_no_source_conflict_markers.sh"
    "check_no_changelog_conflict_markers.sh"
    "check_doc_sha_dedup.sh"
    "check_commit_subject_length.sh"
    "check_unique_cmake_source_ownership.py"
    "check_no_process_wide_caches.sh"
    "check_no_runtime_image_decode.sh"
    "check_no_hidden_render_scratch.sh"
    "check_no_dead_shape_type_text.sh"
    "check_no_preset_catalog_magic_statics.sh"
    "check_no_software_effect_dispatch_switch.sh"
    "check_effect_subsystem_ownership.sh"
    "check_no_text_material_external_effects.sh"
    "check_compiled_resource_authority.py"
    "check_asset_lookup_authority.py"
    "check_render_to_media_authority.py"
    "check_architecture.py"
    "check_architecture_boundaries.sh"
)

CHRONON3D_WBH_GATES=(
    "verify_cli_render_surface_linux.sh"
    "verify_sdk_consumer_functional_linux.sh"
    "check_determinism.sh"
    "check_determinism_matrix.sh"
)
