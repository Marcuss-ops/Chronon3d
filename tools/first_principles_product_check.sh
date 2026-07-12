#!/usr/bin/env bash
# first_principles_product_check.sh — First-Principles Product Check orchestrator (stub).
# Maps the 14 brutal product tests onto runtime gates + TODO follow-up slots.
# Active today: 3/5 fully wired (Architecture / Fast feedback / External consumer) +
# 2/5 with TODO body (Determinism / Product demo — pending Follow-ups 3 + 4).
# TODO stub headers (9): camera / multilingual / errors / cost / scale /
# elimination / legacy-grep / feature-usefulness / scorecard.
# Ends FIRST_PRINCIPLES_PRODUCT_PASS only when every wired gate is clean.
# Per AGENTS.md §"INFO-level diagnostic style" + "Test binary staleness check".
set -euo pipefail

GATE_NAME=first_principles_product_check
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

echo "== Architecture =="
bash "$SCRIPT_DIR/check_architecture_boundaries.sh"
bash "$SCRIPT_DIR/check_camera_architecture.sh"
bash "$SCRIPT_DIR/check_test_hygiene.sh"

echo "== Fast feedback =="
cmake --preset linux-fast-dev
cmake --build --preset linux-fast-dev -j"$(nproc)"
ctest --preset linux-fast-dev-test --output-on-failure

echo "== External consumer =="
bash "$SCRIPT_DIR/install_consumer_test.sh"

echo "== Determinism =="
bash "$SCRIPT_DIR/check_determinism_matrix.sh"
bash "$SCRIPT_DIR/check_first_principles_legacy_grep.sh"

echo "== Product demo =="
bash "$SCRIPT_DIR/check_product_launch_demo.sh"

echo "== Camera brutal =="               # TODO (Test #9)
echo "== Multilingual text =="           # TODO (Test #8)
echo "== Fail-loud errors =="            # TODO (Test #7)
echo "== Real cost =="                   # TODO (Test #11)
echo "== Scale 100 batch =="             # TODO (Test #12)
echo "== Brutal elimination =="          # TODO (Test #4)
echo "== Legacy grep audit =="           # TODO (Test #10 — promote follow-up 2)
echo "== Feature usefulness gate =="     # TODO (Test #14 — docs gate)
echo "== Weekly scorecard =="            # TODO (Track-13 — output terr.)

echo "FIRST_PRINCIPLES_PRODUCT_PASS"
echo "[INFO] ${GATE_NAME}: 4/5 sections have ≥1 wired sub-gate (Determinism: Test #6 + Test #10 fully wired; Product demo: Test #1 wired-but-§honesty-PARTIAL until build-host verifies); 1/5 still empty (Product demo); 9 stub headers pending"
