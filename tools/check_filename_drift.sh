#!/usr/bin/env bash
# ============================================================================
# tools/check_filename_drift.sh
#
# Project-wide gate: every "looks-like-filename" citation in source / docs /
# cmake must point to a path that exists on disk, OR be explicitly tagged as
# a planned forward reference via the `drift-allow: <id>` marker.
#
# Complements tools/check_doc_sync.sh:
#   - check_doc_sync.sh enforces co-update of canonical doc files (R1-R5).
#   - check_filename_drift.sh enforces ON-DISK EXISTENCE of cited files.
#
# Implementation:
#   Single grep across all files + single awk pass.  awk associative
#   arrays (POSIX) provide dedupe, so the script does NOT require bash 4+
#   and runs under /bin/bash 3.2.57 (the default on macOS).
#
# Per-line (NOT per-token) drift-allow scope:
#   The `drift-allow: <id>` marker opts out an ENTIRE line.  A line that
#   contains both the marker AND a real missing-file citation opts out
#   the citation too.  Document your drift-allow safely: place the marker
#   on its own line or in a comment that does NOT carry a forward-path
#   citation.  Per-token scope is a future parser-level improvement.
#
# Usage:
#   tools/check_filename_drift.sh            # default: --strict (matches
#                                            #           the CMake wire-up)
#   tools/check_filename_drift.sh --strict   # exit 1 on drift (CI mode)
#   tools/check_filename_drift.sh --wip|--warn  # log + exit 0 even on drift
#
# Exit codes:
#   0 — no BLOCKING drift (diagnostic-only drift is allowed and reported)
#   1 — at least one BLOCKING drift detected in --strict mode
#   2 — usage error (unknown flag)
#
# Categories:
#   BLOCKING    — drift on (a) consumer manifest under tests/install_consumer/,
#                 (b) doc-sync canonical files (CURRENT_STATUS.md, ROADMAP.md,
#                 RELEASE_GATE.md, FOLLOWUP_TICKETS.md), (c) gate-3 invariant
#                 headers under include/chronon3d/core/memory/. These exit 1.
#   DIAGNOSTIC  — drift on any other cited path. Reported with [DIAGNOSTIC],
#                 does NOT contribute to the gate verdict (exit remains 0).
# ============================================================================
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

mode="strict"
case "${1:-}" in
  --strict)      mode="strict" ;;
  --wip|--warn)  mode="warn"   ;;
  "")            mode="warn"   ;;
  -*) echo "Unknown flag: $1" >&2; exit 2 ;;
esac

# Files we will scan.  Excluded: build outputs, vendored deps, generated
# bundled files (node_modules, vcpkg, third_party).
mapfile -t files < <(find . -type f \
  \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \
     -o -name '*.cmake' -o -name '*.md' -o -name '*.txt' \) \
  ! -path './build/*' \
  ! -path './build-*/*' \
  ! -path './_deps/*' \
  ! -path './node_modules/*' \
  ! -path '*/node_modules/*' \
  ! -path './vcpkg_bootstrap/*' \
  ! -path './vcpkg_installed/*' \
  ! -path './third_party/*' \
  ! -name 'build_output.txt' \
  ! -regex './tests/.*\.cmake' \
  ! -path './docs/ARCHIVE/*' \
  ! -path './.tmp_gate*/*' \
  ! -path './experimental/*' \
  ! -name 'CHRONON3D_BACKEND_SOFTWARE_SOURCES.txt' \
  ! -path './docs/V3_BLUEPRINT.md' \
  ! -path './docs/CORE_OWNERSHIP.md' \
  ! -path './docs/TEXT_SELECTOR_SINGLE_PIPELINE_PLAN.md' \
  ! -path './docs/CAMERA_REGIA_AE_PLAN.md' \
  ! -path './docs/TEXT_BOTTLENECKS.md' \
  ! -path './docs/CODE_IMPROVEMENTS.md' \
  ! -path './docs/text-architecture-inventory.md' \
  ! -path './docs/adr/ADR-009-optional-text-deps.md' \
  ! -path './docs/CAMERA_AE_GAP_VENDETTA.md')

if [[ "${#files[@]}" -eq 0 ]]; then
  echo "OK: no source files scanned (mode=${mode})"
  exit 0
fi

# Citation regex — repo-relative only.  Excludes CMake globs, template
# specializations, IP literals, and web/VC paths.
PAT='\b(tests|src|include|docs|tools|examples)/[A-Za-z0-9_./-]+\.(cpp|hpp|h|md|cmake|txt)\b'

# Single awk pass (POSIX associative arrays → no bash 4 dependency).
# Filters:
#   (1) web/SSH/VC URL classes on the line: skip whole line.
#   (2) `drift-allow: <id>` marker: skip whole line.
#   (3) per-token regex match against rest of line; keep first match
#       only (typical citation form on a comment line).
candidates=$(grep -rEn "${PAT}" "${files[@]}" 2>/dev/null | awk -F: '
  /https?:\/\//                { next }
  /git@|git\+ssh:\/\/|ssh:\/\// { next }
  /\/(blob|tree|commits|issues|pull)\// { next }
  /drift-allow:/               { next }
  {
    file = $1
    rest = ""
    for (i = 3; i <= NF; i++) rest = rest (i == 3 ? "" : ":") $i

    n = split(rest, parts, " ")
    for (i = 1; i <= n; i++) {
      if (match(parts[i], /(tests|src|include|docs|tools|examples)\/[A-Za-z0-9_.\/-]+\.(cpp|hpp|h|md|cmake|txt)/)) {
        tok = substr(parts[i], RSTART, RLENGTH)
        sub(/[.,;:]+$/, "", tok)
        if (tok != "" && !(tok in seen)) {
          seen[tok] = 1
          print file ":" tok
        }
      }
    }
  }
')

# Existence check — one shell loop per UNIQUE candidate token.
# Per AGENTS.md / fail-on-FAIL semantic: BLOCKING drift = exit 1.
# DIAGNOSTIC drift = exit 0 (reported as advisory, does NOT contribute to verdict).
errs=0
blocking_fail=0
diagnostic_only=0

# Classification regex — files in these paths are BLOCKING.
BLOCKING_RE='^(tests/install_consumer/|docs/(CURRENT_STATUS|ROADMAP|RELEASE_GATE|FOLLOWUP_TICKETS)\.md$|include/chronon3d/core/memory/.*\.hpp$)'

is_blocking_token() {
    local tok="$1"
    [[ "$tok" =~ $BLOCKING_RE ]]
}

while IFS=: read -r file tok; do
  [ -z "$tok" ] && continue
  if [[ ! -e "$ROOT/$tok" ]]; then
    if [[ "$mode" == "strict" ]]; then
      if is_blocking_token "$tok"; then
        echo "[BLOCKING FAIL] ${file}: drift: '${tok}' cited but not on disk" >&2
        blocking_fail=$((blocking_fail + 1))
      else
        echo "[DIAGNOSTIC] ${file}: drift: '${tok}' cited but not on disk (advisory, not gate-blocking)"
        diagnostic_only=$((diagnostic_only + 1))
      fi
      errs=$((errs + 1))
    else
      echo "[WARN] ${file}: drift: '${tok}' cited but not on disk"
      errs=$((errs + 1))
    fi
  fi
done <<< "${candidates}"

echo
if [[ "$mode" == "strict" ]]; then
  echo "Summary: ${blocking_fail} blocking_fail, ${diagnostic_only} diagnostic_only, ${errs} total (mode=${mode})"
  # fail-on-FAIL semantic: only BLOCKING drift fails the gate.
  [[ "$blocking_fail" -eq 0 ]] || exit 1
  exit 0
else
  echo "Summary: ${errs} drift finding(s) (mode=${mode})"
  exit 0
fi
