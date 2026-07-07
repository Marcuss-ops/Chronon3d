#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# tools/ae_parity_referee.sh — AE-parity referee (TICKET-AE-PARITY-DRIVER)
#
# Compares per cinematic scene:
#   reference/after_effects/scene_NNN_frame_NN.png   (AE-side mock)
#   reference/chronon3d/scene_NNN_frame_NN.png       (engine output)
# using MAE (mean absolute error) + PSNR (perceptual color metric).
#
# Thresholds (locked, env-overridable — read by the Python helper, not bash):
#   * MAE  <  AE_PARITY_MAE_THRESHOLD_255  (default 5;  units: 0..255)
#   * PSNR >  AE_PARITY_PSNR_THRESHOLD     (default 30; units: dB)
#
# Anti-greenwashing gate:
#   exit 0 ONLY if >= AE_PARITY_MIN_PASS scenes PASS (default 10/15).
#   The "claim AE-like" verdict is REJECTED until the gate clears.
#
# Conventions (AGENTS.md V0.1, TICKET-AE-PARITY-DRIVER):
#   * Pure bash orchestrator (sibling of tools/test_golden_driver.sh).
#   * Python+Pillow is the only mandatory external dep (already in dev env).
#   * No new public API surface in include/chronon3d/; no new
#     registry/resolver/cache; no ImageMagick required.
#   * No shell injection: the Python helper does the verdict internally
#     and emits pipe-separated output; bash NEVER interpolates JSON values
#     into `python3 -c` source.  Thresholds come from env vars in Python.
#
# Usage:
#   tools/ae_parity_referee.sh                  # default: run mode
#   tools/ae_parity_referee.sh run              # explicit run mode
#   tools/ae_parity_referee.sh --json           # JSON output for CI
#   tools/ae_parity_referee.sh run --json       # combinable (v2 fix)
#   tools/ae_parity_referee.sh scaffold         # create reference/ skeleton
#   AE_PARITY_MIN_PASS=12 tools/ae_parity_referee.sh   # override gate
#
# Exit codes:
#   0 — anti-greenwashing gate cleared (>= MIN_PASS scenes PASS)
#   1 — anti-greenwashing gate blocked (fewer scenes PASS, OR any FAIL, OR no refs)
#   2 — preconditions not met (missing dirs, missing Python+Pillow, etc.)
# ═══════════════════════════════════════════════════════════════════════════

set -eo pipefail

# Note: we deliberately do NOT use `set -u` (nounset). The script iterates
# over associative arrays via `for k in $(printf "%s\n" "${!arr[@]}" | sort)`;
# when the array is empty, `printf "%s\n" ""` produces a single empty
# iteration that would trigger "unbound variable" on `${arr[$k]}`.  All the
# important error paths are caught by explicit precondition checks
# (python3, Pillow, AE_DIR, ENGINE_DIR, HELPER), so nounset is not needed.

# ── 0. Paths + defaults ──────────────────────────────────────────────────
REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$REPO_ROOT"

AE_DIR="${AE_PARITY_AE_DIR:-$REPO_ROOT/reference/after_effects}"
ENGINE_DIR="${AE_PARITY_ENGINE_DIR:-$REPO_ROOT/reference/chronon3d}"
MIN_PASS="${AE_PARITY_MIN_PASS:-10}"
# These two are read by the Python helper from env vars (for the verdict).
# We also echo them in the summary for human readability.
PSNR_THRESHOLD="${AE_PARITY_PSNR_THRESHOLD:-30}"
MAE_THRESHOLD_255="${AE_PARITY_MAE_THRESHOLD_255:-5}"
HELPER="$REPO_ROOT/tools/ae_parity_referee/diff_pixels.py"
JSON_OUT=0

log() { printf "[ae_parity_referee] %s\n" "$*"; }
err() { printf "[ae_parity_referee][ERR] %s\n" "$*" >&2; }

# ── 0.5. do_scaffold (function defined BEFORE subcommand dispatch) ───────
do_scaffold() {
    log "scaffolding reference/ directory structure at $REPO_ROOT/reference/"
    mkdir -p "$REPO_ROOT/reference/after_effects" \
             "$REPO_ROOT/reference/chronon3d"

    cat > "$REPO_ROOT/reference/README.md" <<'README_EOF'
# Reference — AE-parity mock + engine output

This directory hosts the AE-parity referee's input/output PNGs:

- `after_effects/scene_NNN_frame_NN.png` — AE-side mock (manually committed,
  canonical reference; AE = Adobe After Effects).
- `chronon3d/scene_NNN_frame_NN.png`     — engine output (gitignored via
  `reference/chronon3d/*.png` in `.gitignore`, regenerated per ctest run).

## Contract

Per frame: `MAE < 5/255` (mean absolute error) AND `PSNR > 30dB` (perceptual
color metric). Anti-greenwashing: >= 10 of 15 cinematic scenes must PASS
before any "AE-like" claim is allowed.

## Commands

```bash
tools/ae_parity_referee.sh scaffold    # create dirs (idempotent)
tools/ae_parity_referee.sh             # human-readable table
tools/ae_parity_referee.sh --json      # JSON output for CI
AE_PARITY_MIN_PASS=15 tools/ae_parity_referee.sh  # tighten the gate
```

## Cross-link

- TICKET-AE-PARITY-DRIVER (this script + ticket doc)
- TICKET-AE-PARITY-SUITE (the 20 cinematic scenes that fill reference/after_effects/)
- TICKET-AE-PARITY-FLOOR (288+ PNG floor deliverable)
README_EOF

    touch "$REPO_ROOT/reference/after_effects/.gitkeep" \
          "$REPO_ROOT/reference/chronon3d/.gitkeep"

    log "created: $REPO_ROOT/reference/ (README.md + 2 dirs + 2 .gitkeep)"
    log "next: commit AE-side mock PNGs to reference/after_effects/"
    log "      populate reference/chronon3d/ via ctest or direct render"
}

# ── 1. Subcommand dispatch (v2: loop over $@, --json combinable) ─────────
for arg in "$@"; do
    case "$arg" in
        run)
            : # explicit run mode, no-op
            ;;
        --json)
            JSON_OUT=1
            ;;
        scaffold)
            do_scaffold
            exit 0
            ;;
        -h|--help|help)
            sed -n '2,38p' "$0"
            exit 0
            ;;
        *)
            err "unknown subcommand: $arg"
            err "  try: tools/ae_parity_referee.sh --help"
            exit 2
            ;;
    esac
done

# ── 2. Precondition checks (must precede any scoring) ───────────────────
if ! command -v python3 >/dev/null 2>&1; then
    err "python3 not found in PATH"
    err "  fix: install python3 (apt install python3 / brew install python3)"
    exit 2
fi
if ! python3 -c 'from PIL import Image' 2>/dev/null; then
    err "Python+Pillow required (Pillow >= 10)"
    err "  fix: pip install Pillow (or apt install python3-pil)"
    exit 2
fi
if [ ! -d "$AE_DIR" ]; then
    err "AE reference dir missing: $AE_DIR"
    err "  fix: tools/ae_parity_referee.sh scaffold"
    exit 1
fi
if [ ! -d "$ENGINE_DIR" ]; then
    err "Engine output dir missing: $ENGINE_DIR"
    err "  fix: tools/ae_parity_referee.sh scaffold"
    exit 1
fi
if [ ! -f "$HELPER" ]; then
    err "Helper script missing: $HELPER"
    err "  fix: git status; if file is missing, restore from commit history"
    exit 2
fi

# ── 3. Discovery ─────────────────────────────────────────────────────────
shopt -s nullglob
declare -A scene_frames   # scene_NNN -> space-separated frame_NN list
for f in "$AE_DIR"/scene_*_frame_*.png; do
    base="$(basename "$f")"
    if [[ "$base" =~ ^scene_([0-9]+)_frame_([0-9]+)\.png$ ]]; then
        scene_num="${BASH_REMATCH[1]}"
        frame_num="${BASH_REMATCH[2]}"
        scene_frames["$scene_num"]="${scene_frames[$scene_num]:-} ${frame_num}"
    fi
done

if [ "${#scene_frames[@]}" -eq 0 ]; then
    err "no scene_NNN_frame_NN.png found in $AE_DIR/"
    err "  fix: commit AE-side mock PNGs to $AE_DIR/ then re-run"
    err "       (1+ frame per scene, naming pattern: scene_NNN_frame_NN.png)"
    exit 1
fi

# ── 4. Per-frame diff + per-scene aggregation ───────────────────────────
declare -A scene_results  # scene_NNN -> PASS|FAIL|SKIP
total_frames=0
total_pass=0
total_fail=0
total_skip=0

if [ "$JSON_OUT" = "0" ]; then
    printf "%-12s %-8s %-12s %-10s %s\n" "SCENE" "FRAME" "MAE(0-255)" "PSNR(dB)" "STATUS"
    printf "%-12s %-8s %-12s %-10s %s\n" "-----" "-----" "---------" "--------" "------"
fi

for scene_num in $(printf "%s\n" "${!scene_frames[@]}" | sort); do
    scene_pass=1
    scene_skip=0
    # NOTE: do NOT wrap ${scene_frames[$scene_num]} in $() — that would
    # try to run the frame list as a command. We just want word-split.
    for frame_num in ${scene_frames[$scene_num]}; do
        ae_path="$AE_DIR/scene_${scene_num}_frame_${frame_num}.png"
        eng_path="$ENGINE_DIR/scene_${scene_num}_frame_${frame_num}.png"
        total_frames=$((total_frames + 1))

        if [ ! -f "$eng_path" ]; then
            total_skip=$((total_skip + 1))
            scene_skip=1
            if [ "$JSON_OUT" = "0" ]; then
                printf "%-12s %-8s %-12s %-10s %s\n" "scene_$scene_num" "f$frame_num" "-" "-" "SKIP"
            fi
            continue
        fi

        # v2: Python helper emits the verdict directly (no shell injection).
        # Output is "OK|mae|psnr|blank|verdict|reasons" or "ERR|type|detail".
        result_line="$(python3 "$HELPER" "$ae_path" "$eng_path" 2>/dev/null)" || \
            result_line="ERR|helper_failed|?"

        case "$result_line" in
            OK\|*\|*\|*\|*\|*)
                mae_255="$(echo "$result_line" | cut -d'|' -f2)"
                psnr_db="$(echo "$result_line" | cut -d'|' -f3)"
                blank="$(echo "$result_line" | cut -d'|' -f4)"
                verdict="$(echo "$result_line" | cut -d'|' -f5)"
                # reasons field is informational only (debug aid); not used in gate.
                if [ "$verdict" = "PASS" ]; then
                    total_pass=$((total_pass + 1))
                    status="PASS"
                else
                    total_fail=$((total_fail + 1))
                    scene_pass=0
                    status="FAIL"
                fi
                if [ "$JSON_OUT" = "0" ]; then
                    printf "%-12s %-8s %-12s %-10s %s\n" "scene_$scene_num" "f$frame_num" "$mae_255" "$psnr_db" "$status"
                fi
                ;;
            ERR\|*\|*)
                err_type="$(echo "$result_line" | cut -d'|' -f2)"
                total_fail=$((total_fail + 1))
                scene_pass=0
                if [ "$JSON_OUT" = "0" ]; then
                    printf "%-12s %-8s %-12s %-10s %s\n" "scene_$scene_num" "f$frame_num" "-" "-" "FAIL($err_type)"
                fi
                ;;
            *)
                total_fail=$((total_fail + 1))
                scene_pass=0
                if [ "$JSON_OUT" = "0" ]; then
                    printf "%-12s %-8s %-12s %-10s %s\n" "scene_$scene_num" "f$frame_num" "-" "-" "FAIL(malformed_helper_output)"
                fi
                ;;
        esac
    done

    if [ "$scene_skip" = "1" ] && [ "$scene_pass" = "1" ]; then
        scene_results["$scene_num"]="SKIP"
    elif [ "$scene_pass" = "1" ]; then
        scene_results["$scene_num"]="PASS"
    else
        scene_results["$scene_num"]="FAIL"
    fi
done

# ── 5. Aggregate scene-level results ─────────────────────────────────────
pass_scenes=0
fail_scenes=0
skip_scenes=0
for scene_num in $(printf "%s\n" "${!scene_frames[@]}" | sort); do
    case "${scene_results[$scene_num]:-SKIP}" in
        PASS) pass_scenes=$((pass_scenes + 1)) ;;
        FAIL) fail_scenes=$((fail_scenes + 1)) ;;
        SKIP) skip_scenes=$((skip_scenes + 1)) ;;
    esac
done

# ── 6. Final verdict ─────────────────────────────────────────────────────
if [ "$JSON_OUT" = "0" ]; then
    echo ""
    echo "============================================"
    echo "AE-parity referee summary (TICKET-AE-PARITY-DRIVER)"
    echo "============================================"
    echo "Scenes:         ${#scene_frames[@]} discovered (${pass_scenes} PASS / ${fail_scenes} FAIL / ${skip_scenes} SKIP)"
    echo "Frames:         ${total_frames} (${total_pass} PASS / ${total_fail} FAIL / ${total_skip} SKIP)"
    echo "Thresholds:     MAE < ${MAE_THRESHOLD_255}/255 AND PSNR > ${PSNR_THRESHOLD}dB"
    echo "Anti-greenwash: PASS scenes must be >= ${MIN_PASS} (current: ${pass_scenes})"
    echo ""
    if [ "$pass_scenes" -ge "$MIN_PASS" ]; then
        echo "VERDICT: PASS — anti-greenwashing gate CLEARED"
        exit 0
    else
        echo "VERDICT: FAIL — anti-greenwashing gate BLOCKED (need ${MIN_PASS}, got ${pass_scenes})"
        exit 1
    fi
else
    verdict="FAIL"
    [ "$pass_scenes" -ge "$MIN_PASS" ] && verdict="PASS"
    cat <<EOF
{
  "scenes": ${#scene_frames[@]},
  "pass_scenes": ${pass_scenes},
  "fail_scenes": ${fail_scenes},
  "skip_scenes": ${skip_scenes},
  "total_frames": ${total_frames},
  "pass_frames": ${total_pass},
  "fail_frames": ${total_fail},
  "skip_frames": ${total_skip},
  "min_pass_required": ${MIN_PASS},
  "thresholds": {
    "mae_255_threshold": ${MAE_THRESHOLD_255},
    "psnr_db_threshold": ${PSNR_THRESHOLD}
  },
  "verdict": "${verdict}"
}
EOF
    [ "$verdict" = "PASS" ] && exit 0 || exit 1
fi
