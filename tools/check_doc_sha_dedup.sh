#!/usr/bin/env bash
# ============================================================================
# tools/check_doc_sha_dedup.sh
#
# CI gate for the TICKET-FOLLOWUP-DE-DUP-REFERENCES macchina-verifica recipe.
#
# Scans `docs/adr/*.md` for (file, sha_7_char_prefix) cite pairs with
# > 1 line-occurrence.  Filters the EXEMPT pairs identified in the ticket:
#   - ADR-015/c03ce2a2 (EXEMPT_SPECIAL_CASE): single-SHA 17x cites are the
#     corpus-consolidation ADR for the matrix-fix commit (structurally required).
#   - ADR-016 (LEGITIMATE_MULTI_CITE): multi-SHA multi-role cite (33798b0a,
#     0f47d591, fab2046e, 7eb5c2ba, a0fbc57b) with distinct semantic roles.
# Also skips INDEX.md (cross-references only, not a dedup target).
#
# Exits 0 if no NON-EXEMPT duplicate pair (gate PASS).
# Exits 1 if at least one non-EXEMPT duplicate pair (gate FAIL).
#
# This gate pins the closure criterion of TICKET-FOLLOWUP-DE-DUP-REFERENCES
# in CI without waiting for the remaining 10 forward-point atomic commits.
# A FAIL here is the actual signal that the corpus is not yet dedup-clean.
#
# Usage:  tools/check_doc_sha_dedup.sh
# Exit codes: 0 = gate PASS, 1 = gate FAIL.
# ============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

# EXEMPT list (per TICKET-FOLLOWUP-DE-DUP-REFERENCES Confine + EXEMPT_SPECIAL_CASE/LEGITIMATE_MULTI_CITE)
EXEMPT_ADRS=(
    "ADR-015"  # single-SHA / c03ce2a2 cited 17x; corpus-consolidation ADR
    "ADR-016"  # multi-SHA / multi-role structural cite (33798b0a/0f47d591/fab2046e/7eb5c2ba/a0fbc57b)
)
SKIP_FILES=("INDEX.md")  # INDEX is a pure cross-ref index, not a dedup target

echo "================================================================"
echo "TICKET-FOLLOWUP-DE-DUP-REFERENCES macchina-verifica gate"
echo "================================================================"
echo ""
echo "EXEMPT ADRs (full file excluded, all sha7 / any line-count):"
for ex in "${EXEMPT_ADRS[@]}"; do
    echo "  - ${ex}-*.md"
done
echo "SKIP files (cross-ref only):"
for sk in "${SKIP_FILES[@]}"; do
    echo "  - ${sk}"
done
echo ""

PAIR_FILE="$(mktemp -t check_doc_sha_dedup.XXXXXX)"
DUP_FILE="$(mktemp -t check_doc_sha_dedup_dup.XXXXXX)"
trap 'rm -f "${PAIR_FILE}" "${DUP_FILE}"' EXIT

total_pairs=0
shopt -s nullglob
for f in docs/adr/*.md; do
    [[ -f "$f" ]] || continue
    base="$(basename "$f")"

    skip=0
    for sk in "${SKIP_FILES[@]}"; do
        [[ "${base}" == "${sk}" ]] && { skip=1; break; }
    done
    [[ "${skip}" -eq 1 ]] && continue

    skip=0
    for ex in "${EXEMPT_ADRS[@]}"; do
        if [[ "${base}" == "${ex}-"* ]]; then
            skip=1
            break
        fi
    done
    if [[ "${skip}" -eq 1 ]]; then continue; fi

    while IFS= read -r kv; do
        [[ -z "${kv}" ]] && continue
        [[ "${kv}" =~ ^([^:]+):([0-9]+):(.*)$ ]] || continue
        content="${BASH_REMATCH[3]}"
        shas="$(echo "${content}" | grep -oE '[0-9a-f]{7,40}' || true)"
        for sha in ${shas}; do
            [[ -z "${sha}" ]] && continue
            echo "${base}:${sha:0:7}" >> "${PAIR_FILE}"
            total_pairs=$((total_pairs + 1))
        done
    done < <(git grep -nE '[0-9a-f]{7,}' -- "${f}" 2>/dev/null || true)
done
shopt -u nullglob 2>/dev/null || true

if [[ ! -s "${PAIR_FILE}" ]]; then
    echo "Total pairs : 0 occurrences"
    echo "Duplicates  : 0"
    echo ""
    echo "OK: no SHA-cite instances in non-EXEMPT ADR files"
    echo "DEDUP_PASS: macchina-verifica exit gate satisfied"
    exit 0
fi

sort "${PAIR_FILE}" | uniq -c | awk '$1 > 1 {print}' > "${DUP_FILE}" || true
DEDUP_COUNT="$(wc -l < "${DUP_FILE}" | tr -d '[:space:]')"

echo "Total pairs : ${total_pairs} (file, sha7) occurrences"
echo "Duplicates  : ${DEDUP_COUNT} non-EXEMPT pair(s) with > 1 occurrence"
echo ""

if [[ "${DEDUP_COUNT}" -eq 0 ]]; then
    echo "OK: 0 non-EXEMPT duplicate (file, sha7) pairs in docs/adr/"
    echo "DEDUP_PASS: macchina-verifica exit gate satisfied (post-10 forward-point commits)"
    exit 0
fi

echo "FAIL: ${DEDUP_COUNT} non-EXEMPT duplicate (file, sha7) pairs in docs/adr/:"
echo ""
sort -k2 "${DUP_FILE}" | awk '
    {
        cnt = $1; kv = $2;
        n = split(kv, parts, ":");
        file = parts[1];
        sha  = parts[2];
        printf "  %s x %d   in  %s\n", sha, cnt, file;
    }
'
echo ""
echo "Remediation per TICKET-FOLLOWUP-DE-DUP-REFERENCES section Soluzione accettabile item 1:"
echo "  Per-ADR atomic commits; subject convention:"
echo "    docs(adr): ADR-NNN inline-one SHA cite (de-dup COMMIT_SHA)"
echo ""
echo "DEDUP_FAIL: macchina-verifica exit gate violated (exit 1)"
exit 1
