#!/usr/bin/env bash
# Execute one canonical gate using the same argument contract everywhere.
# Usage: bash tools/execute_gate.sh <gate> [remote] [branch]
#
# Interpreter selection (2026-09-12): every *.py gate here reads
# tools/architecture_rules*.toml, but `python3` is 3.10 on some hosts, where
# neither tomllib (stdlib, 3.11+) nor tomli is installed — the whole chain then
# dies with `ModuleNotFoundError: tomli` instead of running the gate. Pick the
# first interpreter that can load TOML, mirroring
# tools/architecture_gate_selftest.sh, with CHRONON_PYTHON as an explicit
# override.  If none qualifies we still fall back to `python3` and let the gate
# report its own import error rather than failing at the dispatcher.

set -euo pipefail

GATE="${1:?gate name is required}"
REMOTE="${2:-origin}"
BRANCH="${3:-$(git rev-parse --abbrev-ref HEAD)}"
ROOT="$(git rev-parse --show-toplevel)"
SCRIPT_DIR="$ROOT/tools"

PYTHON_CANDIDATES=(python3 python3.13 python3.12 python3.11)
PYTHON_BIN="${CHRONON_PYTHON:-}"
if [[ -z "$PYTHON_BIN" ]]; then
    for module in tomllib tomli; do
        for candidate in "${PYTHON_CANDIDATES[@]}"; do
            if command -v "$candidate" >/dev/null 2>&1 \
                && "$candidate" -c "import $module" >/dev/null 2>&1; then
                PYTHON_BIN="$candidate"
                break 2
            fi
        done
    done
fi
if [[ -z "$PYTHON_BIN" ]]; then
    PYTHON_BIN="python3"
fi

case "$GATE" in
  *.py)
    exec "$PYTHON_BIN" "$SCRIPT_DIR/$GATE"
    ;;
  check_commit_subject_length.sh)
    exec bash "$SCRIPT_DIR/$GATE" "$REMOTE/$BRANCH"
    ;;
  check_steady_state_allocations.sh)
    exec bash "$SCRIPT_DIR/gates/$GATE"
    ;;
  *)
    exec bash "$SCRIPT_DIR/$GATE"
    ;;
esac
