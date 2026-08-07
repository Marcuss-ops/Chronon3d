#!/usr/bin/env bash
# Execute one canonical gate using the same argument contract everywhere.
# Usage: bash tools/execute_gate.sh <gate> [remote] [branch]

set -euo pipefail

GATE="${1:?gate name is required}"
REMOTE="${2:-origin}"
BRANCH="${3:-$(git rev-parse --abbrev-ref HEAD)}"
ROOT="$(git rev-parse --show-toplevel)"
SCRIPT_DIR="$ROOT/tools"

case "$GATE" in
  *.py)
    exec python3 "$SCRIPT_DIR/$GATE"
    ;;
  check_commit_subject_length.sh)
    exec bash "$SCRIPT_DIR/$GATE" "$REMOTE/$BRANCH"
    ;;
  check_push_divergence_window.sh)
    exec bash "$SCRIPT_DIR/$GATE" "$REMOTE" "$BRANCH"
    ;;
  *)
    exec bash "$SCRIPT_DIR/$GATE"
    ;;
esac
