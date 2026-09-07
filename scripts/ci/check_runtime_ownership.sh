#!/usr/bin/env bash
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
DOC="$ROOT/docs/architecture/RUNTIME-OWNERSHIP-MATRIX.md"
test -f "$DOC"
awk -F'|' '/^\| [^ -].*\|/ && $0 !~ /Field|------/ {
  count=0; for (i=2; i<=NF-1; ++i) if ($i ~ /✅/) ++count;
  if (count != 1) { print "ownership row must have exactly one owner: "$0; bad=1 }
} END { if (bad) exit 1 }' "$DOC"
echo "runtime ownership: PASS"
