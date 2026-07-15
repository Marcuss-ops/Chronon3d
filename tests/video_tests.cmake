# ── Video Contracts Tests — NEW canonical TICKET-VIDEO-CONTRACTS-BULK ──
#
# 5 TEST_CASEs encoding Video Completeness Matrix §14+§15+§17+§18+§19
# regression lock contract per user spec verbatim. Replaces the prior
# stub (migrated to media_tests.cmake on 2026-07-12) with a real
# INTEGRATION-tier target exercising the ChrononGlowFinalAE +
# AnimTypewriterGlow + ProductLaunch CLI integration paths through
# `chronon3d_cli video --start --end --fps --output`.
#
# Per AGENTS.md §honesty: gracefully skips on env-blocked VPS via
# ffmpeg_available() / discover_cli_binary() preconditions (canonical
# precedent at tests/cli/test_video_adapter_e2e.cpp:42 + tools/check_
# first_principles_fail_loud.sh binary-discovery pattern).
#
# Per-area early-return gate (TICKET-CMAKE-TEST-MANIFEST-UNIFICATION).
# CHRONON3D_ENABLE_VIDEO matches the pre-refactor orchestrator's gate;
# the existing body-wrap below further narrows to CHRONON3D_BUILD_CLI
# which is the actual compile-time requirement.
if(NOT CHRONON3D_ENABLE_VIDEO)
    return()
endif()
set(_video_contracts_link_targets
    chronon3d_cli_render chronon3d_cli_core
    chronon3d_sdk chronon3d_sdk_impl chronon3d_pipeline
    chronon3d_scene chronon3d_backend_software
    chronon3d_media_video chronon3d_backend_image
    CLI11::CLI11 fmt::fmt)
if(TARGET chronon3d_cli_dev)
    list(APPEND _video_contracts_link_targets chronon3d_cli_dev)
endif()

chronon3d_add_test_suite(
    NAME chronon3d_video_contracts_tests
    TIER INTEGRATION
    LINK_TARGETS ${_video_contracts_link_targets}
    SOURCES video/test_video_contracts.cpp
)
target_include_directories(chronon3d_video_contracts_tests
    PRIVATE ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli)
