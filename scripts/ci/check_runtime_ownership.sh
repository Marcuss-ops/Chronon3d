#!/usr/bin/env bash
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
DOC="$ROOT/docs/architecture/RUNTIME-OWNERSHIP-MATRIX.md"
test -f "$DOC"
# Two table shapes carry ownership rows:
#   * the Process/Job/Frame matrix marks the single owner column with ✅;
#   * the "Sole owner" tables name the owner in the second cell.
# Both must resolve to exactly one owner per fact; the schema decides how it
# is spelled, so the gate follows the header instead of assuming one shape.
awk -F'|' '
  /^\| *Field \/ responsibility/ { mode = "columns"; next }
  /^\| *(Axis|Fact) *\| *Sole owner/ { mode = "sole"; next }
  /^\|/ {
    separator = 1
    for (i = 2; i <= NF - 1; ++i)
      if ($i !~ /^[[:space:]:-]*$/) separator = 0
    if (separator) next
    if (mode == "columns") {
      count = 0
      for (i = 2; i <= NF - 1; ++i) if ($i ~ /✅/) ++count
      if (count != 1) {
        print "ownership row must have exactly one owner: " $0
        bad = 1
      }
    } else if (mode == "sole") {
      owner = $3
      gsub(/^[[:space:]]+|[[:space:]]+$/, "", owner)
      count = 0
      for (i = 2; i <= NF - 1; ++i) if ($i ~ /✅/) ++count
      if (owner == "" || count > 1) {
        print "sole-owner row must name exactly one owner: " $0
        bad = 1
      }
    }
    next
  }
  { mode = "" }
  END { if (bad) exit 1 }' "$DOC"
echo "runtime ownership: PASS"
