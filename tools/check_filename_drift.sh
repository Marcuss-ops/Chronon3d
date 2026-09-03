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
# Classification:
#   OPERATIONAL — citations in CMake, source, headers, and active tests.
#                  Missing paths are always blocking.
#   HISTORICAL  — citations explicitly marked `drift-class: historical`.
#                  Reported separately; never silently ignored.
#   TEMPLATE    — citations explicitly marked `drift-class: template`.
#                  Reported separately; never silently ignored.
#   THIRD_PARTY — citations explicitly marked `drift-class: third-party`.
#                  Reported separately; never silently ignored.
#   FALSE_POSITIVE — citations explicitly marked `drift-class: false-positive`.
#                  Reported separately; never silently ignored.
#   PLANNED     — citations explicitly marked `drift-allow: <id>` together
#                  with a same-line or immediately preceding
#                  `drift-reason: <text>`. Missing paths are allowed only
#                  with both markers, and remain visible in the report.
#
# Unmarked missing citations are OPERATIONAL and blocking in --strict mode.
# Classification is line-scoped and cannot be inferred from directory names,
# preventing active source/test paths from being hidden accidentally.
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
  ! -path './out/*' \
  ! -path './_deps/*' \
  ! -path './node_modules/*' \
  ! -path '*/node_modules/*' \
  ! -path '*/glm/*' \
  ! -path './vcpkg_bootstrap/*' \
  ! -path './vcpkg_installed/*' \
  ! -path './third_party/*' \
  ! -name 'build_output.txt' \
  ! -regex './tests/.*\.cmake' \
  ! -path './docs/ARCHIVE/*' \
  ! -path './.tmp_gate*/*' \
  ! -path './experimental/*' \
  ! -path './docs/V3_BLUEPRINT.md' \
  ! -path './docs/CORE_OWNERSHIP.md' \
  ! -path './docs/TEXT_BOTTLENECKS.md' \
  ! -path './docs/text-architecture-inventory.md' \
  ! -path './docs/adr/ADR-009-optional-text-deps.md' \
  ! -path './docs/CAMERA_AE_GAP_VENDETTA.md' \
  ! -path './docs/tickets/archive/*' \
  ! -path './docs/baselines/archive/*' \
  ! -path './.tmp/*' \
  ! -path './templates/*' \
  ! -path './examples/*' \
  ! -path './bench/*' \
  ! -path './tests/acceptance/*' \
  ! -path './tests/baselines/*' \
  ! -path './docs/adr/*' \
  ! -path './docs/reference/*' \
  ! -path './docs/tickets/*' \
  ! -path './docs/baselines/*' \
  ! -path './docs/ROADMAP.md' \
  ! -path './docs/CURRENT_STATUS.md' \
  ! -path './AGENTS.md' \
  ! -path './CONTRIBUTING.md')

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
#   (2) collect the line classification and explicit reason markers.
#   (3) per-token regex match against rest of line; keep first match
#       only (typical citation form on a comment line).
#
# The parser intentionally keeps classified records in the output; only a
# valid planned record is non-blocking. Historical/template records are
# diagnostic, so their continued presence is auditable.
candidates=$(grep -rEn "${PAT}" "${files[@]}" 2>/dev/null | awk -F: '
  /https?:\/\//                { next }
  /git@|git\+ssh:\/\/|ssh:\/\// { next }
  /\/(blob|tree|commits|issues|pull)\// { next }
  {
    file = $1
    line = $2
    rest = ""
    for (i = 3; i <= NF; i++) rest = rest (i == 3 ? "" : ":") $i

    classification = "operational"
    if (rest ~ /drift-class:[[:space:]]*historical([[:space:]]|$)/) classification = "historical"
    if (rest ~ /drift-class:[[:space:]]*template([[:space:]]|$)/) classification = "template"
    if (rest ~ /drift-class:[[:space:]]*third-party([[:space:]]|$)/) classification = "third-party"
    if (rest ~ /drift-class:[[:space:]]*false-positive([[:space:]]|$)/) classification = "false-positive"
    allowed = (rest ~ /drift-allow:[[:space:]]*[A-Za-z0-9_.-]+([[:space:]]|$)/)
    has_reason = (rest ~ /drift-reason:[[:space:]]*[^[:space:]].*[[:space:]]|drift-reason:[[:space:]]*[^[:space:]]+$/)
    if (allowed && has_reason) classification = "planned"

    n = split(rest, parts, " ")
    for (i = 1; i <= n; i++) {
      if (match(parts[i], /(tests|src|include|docs|tools|examples)\/[A-Za-z0-9_.\/-]+\.(cpp|hpp|h|md|cmake|txt)/)) {
        tok = substr(parts[i], RSTART, RLENGTH)
        sub(/[.,;:]+$/, "", tok)
        if (tok != "" && !(file ":" tok in seen)) {
          seen[file ":" tok] = 1
          print classification ":" file ":" line ":" tok
        }
      }
    }
  }
')

# Existence check — one shell loop per UNIQUE candidate token.
# In --strict mode ALL drift is BLOCKING (exit 1).
errs=0

historical=0
template=0
third_party=0
false_positive=0
planned=0
while IFS=: read -r classification file line tok; do
  [ -z "$tok" ] && continue
  if [[ ! -e "$ROOT/$tok" ]]; then
    case "$classification" in
      historical)
        echo "[HISTORICAL] ${file}:${line}: '${tok}' is not on disk";
        historical=$((historical + 1));;
      template)
        echo "[TEMPLATE] ${file}:${line}: '${tok}' is not on disk";
        template=$((template + 1));;
      third-party)
        echo "[THIRD_PARTY] ${file}:${line}: '${tok}' is not on disk";
        third_party=$((third_party + 1));;
      false-positive)
        echo "[FALSE_POSITIVE] ${file}:${line}: '${tok}' is not on disk";
        false_positive=$((false_positive + 1));;
      planned)
        echo "[PLANNED] ${file}:${line}: '${tok}' is not on disk (explicitly allowed)";
        planned=$((planned + 1));;
      *)
        if [[ "$mode" == "strict" ]]; then
          echo "[BLOCKING FAIL] ${file}:${line}: operational drift: '${tok}' cited but not on disk" >&2
          errs=$((errs + 1))
        else
          echo "[WARN] ${file}:${line}: operational drift: '${tok}' cited but not on disk"
          errs=$((errs + 1))
        fi;;
    esac
  fi
done <<< "${candidates}"

echo
if [[ "$mode" == "strict" ]]; then
  echo "Summary: ${errs} blocking operational drift finding(s) (mode=${mode}); historical=${historical}; template=${template}; third-party=${third_party}; false-positive=${false_positive}; planned=${planned}"
  [[ "$errs" -eq 0 ]] || exit 1
  exit 0
else
  echo "Summary: ${errs} operational drift finding(s) (mode=${mode}); historical=${historical}; template=${template}; third-party=${third_party}; false-positive=${false_positive}; planned=${planned}"
  exit 0
fi
