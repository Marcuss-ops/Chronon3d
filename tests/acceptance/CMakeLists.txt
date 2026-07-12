# ============================================================================
# tests/acceptance/CMakeLists.txt
#
# Forward-points 0a/0b/0c of TICKET-ACCEPTANCE-FORENSIC-SURFACE — minimal
# orchestrator that registers the new acceptance forensic-surface test
# executable (forward-point 0c wiring) into the existing
# `chronon3d_acceptance` aggregate via the `acceptance` CTest label.
#
# This subdirectory exists to avoid polluting the existing per-area
# `.cmake` files (text_definition_tests.cmake, etc.) with a single-test
# insertion. AGENTS.md "Fare PR piccole e mirate" + Cat-3 (zero new
# public API) honoured: 1 helper pair + 1 acceptance test + this
# orchestrator (no SDK surface change).
# ============================================================================

# Forward-point 0c — INTEGRATION tier acceptance test carrying the
# `acceptance` CTest label. Joins the chronon3d_acceptance aggregate
# via the label filter (`ctest -L acceptance`). Cat-3 + Cat-5 freeze
# compliant: no SDK surface change, zero new symbols in include/.
chronon3d_add_test_suite(
    NAME chronon3d_acceptance_forensic_surface_tests
    TIER INTEGRATION
    SOURCES ${CMAKE_CURRENT_LIST_DIR}/test_acceptance_forensic_surface.cpp
    LABELS acceptance
)

# ── Test #20 — batch_100_videos acceptance (this commit) ────────────────
# 100-job batch (10 lang × 10 topics × 1 format = 100 jobs), 8 metrics per
# job, 4 PASS-criteria envelopes per user spec. StubBatchRunner is LOCAL
# to the test TU (Cat-3 anti-dup: tests never link apps/chronon3d_cli/utils).
# Canonical schema + 4 envelopes + log path live in
# `configs/batch_100_videos_corpus.yaml`. Companion gate + selftest wired:
#   tools/check_batch_100_videos.sh (pre-push gate at Step 4.5k)
#   tests/tools/selftest_batch_100_videos.sh (4-scenario verdict test).
# Forward-points (NOT in this commit per AGENTS.md "Fare PR piccole e mirate"):
#   - TICKET-BATCH-100-DASHBOARD-WIREUP: /api/pilot/batch_100_videos endpoint
#   - TICKET-BATCH-100-REAL-PATH: drop the stub processor for real renders
#   - TICKET-MANUAL-TOUCHES-DASHBOARD-UX: channel-scope consolidation
chronon3d_add_test_suite(
    NAME chronon3d_batch_100_videos_tests
    TIER INTEGRATION
    SOURCES ${CMAKE_CURRENT_LIST_DIR}/test_batch_100_videos.cpp
    LABELS acceptance
)
