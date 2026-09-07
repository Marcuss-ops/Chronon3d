#!/usr/bin/env bash
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
DOC="$ROOT/docs/architecture/RENDER-SESSION-MIGRATION-MATRIX.md"
test -f "$DOC"

for required in \
  'FrameArena' \
  'ExecutionWorkspaceRing' \
  'FrameHistory' \
  'DirtyHistory.previous_layers' \
  'SessionServices' \
  'TextLayoutCache' \
  'NodeMemoryTracker' \
  'RenderErrorSlot' \
  'prepared_composition' \
  'CompiledProgram'; do
  if ! rg -Fq "$required" "$DOC"; then
    echo "render-session migration matrix missing: $required" >&2
    exit 1
  fi
done

awk -F'|' '/^\| [^ -].*\|/ && $0 !~ /Current field|------/ {
  if (NF != 7) { print "invalid migration row: "$0; bad=1 }
} END { if (bad) exit 1 }' "$DOC"

echo "render-session migration matrix: PASS"
