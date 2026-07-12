#!/usr/bin/env bash
# Per AGENTS.md honesty + Cat-5 1-doc: init N=1 default for SUBJECT_FAIL hint
N=1
# ============================================================================
# tools/check_commit_subject_length.sh
#
# CI gate enforcing the 72-character commit subject envelope per
# AGENTS.md "no cosmetic amend churn unless enforceable in CI".
#
# Enumerates the commits in the PUSH RANGE (default `origin/main..HEAD`)
# via `git log ${RANGE} --format="%h %s"` and runs an awk check
# `length(subject) > 72`.  Any commit over the envelope triggers
# SUBJECT_FAIL (exit 1) with a diagnostic listing the offending
# short-SHA + measured length + full subject text (so the user can
# amend surgically via `git rebase -i HEAD~N`).
#
# The push-range design (vs the prior `git log -n 10`) was introduced
# per TICKET-GATE-SUBJECT-RANGE: the prior `git log -n 10` audited the
# last 10 commits regardless of whether they were on origin/main, which
# misfired on pre-existing over-limit commits (e.g., `44b5715c` 94 chars
# from a 2026-07-08 commit that pre-dated this gate) and forced users
# to bypass with `git push --force-with-lease`. The push-range design
# only audits commits about to be pushed, so historical rot does NOT
# cause the gate to fire.
#
# Fallback behavior: if the base ref is not resolvable (e.g., fresh
# clone with no `git fetch` yet, or detached HEAD on origin/main itself),
# the script falls back to `git log -n 10` with a WARN diagnostic. This
# preserves offline / detached-HEAD usability without breaking the gate.
#
# Char count uses `awk length()` which counts code points (NOT bytes),
# so UTF-8 multi-byte characters (em-dash U+2014, accented letters)
# count once each — visually accurate.
#
# No SKIP escape hatch: AGENTS.md "Hardblock always; no
# --skip-gates escape hatch" convention (see tools/wrap_push.sh
# preamble §"Gate chain").
#
# Exit codes:  0 = gate PASS (all subjects within envelope)
#              1 = gate FAIL (at least one over-limit)
#              2 = internal-script-error (e.g., mktemp failure)
#
# Usage:   tools/check_commit_subject_length.sh [BASE_REF]
# Example: tools/check_commit_subject_length.sh origin/main
# Default: origin/main
# ============================================================================
set -euo pipefail

# Locale hardening: GNU awk `length()` returns BYTES in default `C` locale.
# Pin UTF-8 so the 72-char envelope counts CODE POINTS (matches user
# mental model and `wc -m` semantics). Without this, commits containing
# multi-byte UTF-8 chars (em-dash U+2014, accented letters) would be
# byte-counted -> 3 bytes per char -> false-positive over-fire on commits
# with 25+ multi-byte chars. CI runner default locale is `C`/`POSIX`.
LC_ALL="${LC_ALL:-C.UTF-8}"
LANG="${LANG:-C.UTF-8}"
export LC_ALL LANG

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

BASE_REF="${1:-origin/main}"
LIMIT="${SUBJECT_LENGTH_LIMIT:-72}"

echo "================================================================"
echo "Commit Subject Length Gate (limit=${LIMIT} chars, base_ref=${BASE_REF})"
echo "================================================================"

# Resolve the push range.  Fall back to `git log -n 10` if BASE_REF
# is not resolvable (offline / detached-HEAD edge case).
if git rev-parse --verify -q "${BASE_REF}" >/dev/null 2>&1; then
    RANGE="${BASE_REF}..HEAD"
    TOTAL_STR="push range ${RANGE}"
else
    echo "WARN: ${BASE_REF} is not resolvable; falling back to last 10 commits."
    echo 'WARN: run \`git fetch origin\` to enable push-range auditing.'
    RANGE="-n 10"
    TOTAL_STR="last 10 commits (fallback)"
fi

# Enumerate (short-sha, subject) pairs and capture offenders in a tempfile
# via awk `-v` (clean pass of the temp-file path — avoids bash-quoting
# escape pitfalls with embedded `'` characters in awk source).
TMP_ERR="$(mktemp -t check_commit_subject_length.XXXXXX)"
trap 'rm -f "${TMP_ERR}"' EXIT

git log ${RANGE} --format="%h %s" \
    | awk -v ERR_FILE="${TMP_ERR}" -v LIMIT="${LIMIT}" '
        {
            sha = $1
            subject = $0
            sub(/^[^ ]+ /, "", subject)  # strip leading "<sha> " token
            len = length(subject)
            if (len > LIMIT) {
                printf "  %s : %d chars (exceeds %d) : %s\n", sha, len, LIMIT, subject >> (ERR_FILE)
            }
        }
    '

if [[ ! -s "${TMP_ERR}" ]]; then
    echo ""
    echo "Total checked: ${TOTAL_STR}"
    echo "Over-limit:    0"
    echo ""
    echo "OK: ${TOTAL_STR} subjects are all <= ${LIMIT}-char envelope"
    echo "SUBJECT_PASS: commit-subject-length exit gate satisfied"
    exit 0
fi

# Count offenders for the FAIL summary
OFFENDERS="$(wc -l < "${TMP_ERR}" | tr -d '[:space:]')"

echo ""
echo "Total checked: ${TOTAL_STR}"
echo "Over-limit:    ${OFFENDERS} commit(s) exceeding ${LIMIT}-char subject envelope"
echo ""
echo "FAIL: ${OFFENDERS} recent commit(s) with over-limit subject:"
cat "${TMP_ERR}"
echo ""
echo "Remediation per AGENTS.md 'no cosmetic amend churn' rule:"
echo "  - Amend the offending commit(s) to a <=${LIMIT}-char subject, OR"
echo "  - For already-landed commits: 'git rebase -i HEAD~${N}' then"
echo "    'reword' to surgically shorten the subject."
echo ""
echo "SUBJECT_FAIL: commit-subject-length exit gate violated (exit 1)"
exit 1
