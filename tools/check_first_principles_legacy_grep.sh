#!/usr/bin/env bash
# check_first_principles_legacy_grep.sh — Test #10 (nessun legacy) hard-zero grep gate.
# Scans include/ src/ content/ apps/ examples/ for 6 legacy symbols (literal --fixed-strings).
# Exit 1 on ANY hit in productive paths; exit 0 only when 0 hits per symbol.
# docs/ARCHIVE/** excluded by virtue of not being in the productive pathspec (documented for §honesty).
# Per AGENTS.md §"INFO-level diagnostic style" emits `[INFO] ${GATE_NAME}: ...` addizionale
# al canonico `GATE_PASS:` finale sul clean state. FAIL path invariato: GATE_FAIL.
set -euo pipefail

GATE_NAME=check_first_principles_legacy_grep
REPO_ROOT="$(git rev-parse --show-toplevel)"
cd "$REPO_ROOT"

PROD_PATHSPECS=(include src content apps examples)
LEGACY_PATTERNS=(
    "AnimatedCamera2_5D"
    "CameraShotProfile"
    "camera_rig("
    "centered_text("
    "glow_text("
    "current_path()"
)

TOTAL_OFFENDERS=0
FAIL_DETAILS=()

for PATTERN in "${LEGACY_PATTERNS[@]}"; do
    HITS=$(git grep -n --fixed-strings "$PATTERN" -- "${PROD_PATHSPECS[@]}" 2>/dev/null || true)
    if [ -n "$HITS" ]; then
        N=$(printf '%s\n' "$HITS" | wc -l)
        TOTAL_OFFENDERS=$((TOTAL_OFFENDERS + N))
        while IFS= read -r line; do
            [ -n "$line" ] && FAIL_DETAILS+=("  [$PATTERN] $line")
        done <<< "$HITS"
    fi
done

if [ "$TOTAL_OFFENDERS" -gt 0 ]; then
    echo "GATE_FAIL: $TOTAL_OFFENDERS productive-path legacy reference(s) found in include/ src/ content/ apps/ examples/" >&2
    echo "  Test #10 zero-legacy invariant requires 0 hits across 6 symbols." >&2
    echo "  Symbols audited: AnimatedCamera2_5D, CameraShotProfile, camera_rig(, centered_text(, glow_text(, current_path()" >&2
    echo "  (Note: docs/ARCHIVE/** excluded — historical documentation, never in productive pathspec)" >&2
    echo "" >&2
    for d in "${FAIL_DETAILS[@]:0:50}"; do
        echo "$d" >&2
    done
    if [ "${#FAIL_DETAILS[@]}" -gt 50 ]; then
        echo "  ... and $(( ${#FAIL_DETAILS[@]} - 50 )) more occurrences" >&2
    fi
    echo "" >&2
    echo "  Remediation: migrate callers to V1 canonical surface (CameraDescriptor + OrbitMotion + PoseTracksSource + current_path_v1) and delete the legacy types per the camera/text legacy-freeze ADRs." >&2
    exit 1
fi

echo "GATE_PASS: 0 productive-path legacy references — all 6 symbols absent in include/ src/ content/ apps/ examples/"
echo "[INFO] ${GATE_NAME}: 0 hits across 6 symbols in 5 prod paths — Test #10 zero-legacy holds"
