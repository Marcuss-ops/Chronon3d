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
# TODO: wire tools/check_determinism_matrix.sh (Test #6)
# TODO: wire tools/check_first_principles_legacy_grep.sh (Test #10)

echo "== Product demo =="
# TODO: wire `chronon render ProductLaunch --props examples/product_launch.json \
#       --output /tmp/chronon-product-proof.mp4` + ffprobe (Test #1)

echo "== Camera brutal =="               # TODO (Test #9)
echo "== Multilingual text =="           # TODO (Test #8)
echo "== Fail-loud errors =="           # Test #7 (Wired via tools/check_first_principles_fail_loud.sh)
bash "$SCRIPT_DIR/check_first_principles_fail_loud.sh"
echo "== Fix speed =="                   # Test #11 cronograph (replaces the prior "Real cost" mapping per the user spec verbatim "Cronometro del fix"; wired-but-§honesty-zero-data on this VPS until a working build host adds a real JSONL entry)
bash "$SCRIPT_DIR/check_fix_cronograph.sh"
echo "== Scale 100 batch =="             # TODO (Test #12)
echo "== Brutal elimination =="          # TODO (Test #4)
echo "== Legacy grep audit =="           # TODO (Test #10 — promote follow-up 2)
echo "== Feature usefulness gate =="     # TODO (Test #14 — docs gate)
echo "== Weekly scorecard =="            # TODO (Track-13 — output terr.)

echo "FIRST_PRINCIPLES_PRODUCT_PASS"
echo "[INFO] ${GATE_NAME}: 5/6 active sections wired (... Fix speed/Test #11 wired-but-§honesty-zero-data-on-VPS until build-host logs entry; Fail-loud/Test #7 NEWLY wired via tools/check_first_principles_fail_loud.sh); 1/6 TODO-body (Determinism + Product demo); 7 follow-up stub headers pending (Test #4, #8, #9, #12-14, Track-13)"
