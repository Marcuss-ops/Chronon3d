#!/usr/bin/env bash
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
TARGETS=("$ROOT/src" "$ROOT/include" "$ROOT/apps")
PATTERN='execute_command_plan|command_plan_executor'

if rg -n "$PATTERN" "${TARGETS[@]}" --glob '*.{hpp,cpp,inc,cxx,cmake}' ; then
    echo "legacy command-plan executor reference found" >&2
    exit 1
fi

echo "graph executor authority: PASS"
