#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# monitor_push_divergence.sh — cron-friendly divergence monitor
# ═══════════════════════════════════════════════════════════════════════════
#
# Scope:                       Notify WHEN the local-vs-origin delta (LOCALLY ahead)
#                              exceeds a configurable threshold (default 20), with
#                              EXTREME escalation when TRUE divergence is detected.
#                              Converts the today "rebase-at-push emergency" pattern
#                              into an early signal (cron-friendly debounced state
#                              machine), so the operator acts *before* hitting the
#                              pre-push hard-block at tools/check_main_clean.sh.
#
# Cat-3 anti-duplication:      Riusa strumentalmente tools/check_push_divergence_window.sh
#                              (ADR-022 advisory gate) come kernel di calcolo. NON
#                              duplica `git fetch` / `git rev-list` / `git merge-base`.
#
# Cat-5 1-doc same-commit:     Questo script NON modifica i 4 canonical docs.
#                              Tracciamento via docs/FOLLOWUP_TICKETS.md (1 row)
#                              + docs/CHANGELOG.md (1 prepend) — atomic chore.
#
# §honesty:                    exit code 0 SEMPRE in condizioni normali (è advisory).
#                              exit 2 SOLO su internal error (git fetch fallisce,
#                              repo non trovato, tmpfile I/O). Nessuna stima %.
#                              Lo stato è binary HOT/STABLE/EXTREME (categorico).
#
# Rule #2 INFO-level:          Su cron-run end emette riga `[INFO] ${GATE_NAME}: ...`
#                              su stdout per addizionale al canonico `OK:` — il
#                              pattern cron-friendly è silent-by-default + notify-on-
#                              transition (vedi Sezione State Machine).
#
# Cron template (drop-in, user-tunable):
#   */5 * * * *  cd /home/<user>/Chronon3d && tools/monitor_push_divergence.sh
#
#   - Syntax: standard cron(5) USER-crontab format
#   - Cd into repo: required so `git rev-parse --show-toplevel` resolves correctly
#   - Cadence: 5 min default — cron is debounced sufficient transitively; più stretto
#     (1 min) consuma banda fetch SSH/HTTPS, più largo (15 min) perde early signal
#   - Output sink: stderr (cron MAILTO body when [WARN]) + JSONL append (audit tail
#     via dashboard aggregator) + marker file (lane-style check da script esterni)
#
# Env vars:
#   CHRONON3D_MON_THRESHOLD      (integer >=1, default 20).  Andata-soglia per
#                                LOCAL_AHEAD: HOT quando LOCAL_AHEAD > THRESHOLD.
#   CHRONON3D_MON_STATE_DIR      (path, default $HOME/.chronon3d/monitor). Posizione
#                                dei file di stato + JSONL + marker; riusa la
#                                convention Telemetry ~/chronon3d/.
#   REMOTE                       (default origin). Forwarded al gate.
#   BRANCH                       (default main). Forwarded al gate.
#
# State machine (output contract):
#
#   prev  new   notify?  marker   JSONL record (transition=true)   cron mail
#   ────  ────  ───────  ───────  ───────────────────────────────────  ──────────
#   STABLE → STABLE   no     absent   yes (transition=false)         none
#   STABLE → HOT      yes    present  yes (transition=true)          YES (stderr)
#   STABLE → EXTREME  yes    present  yes (transition=true)          YES (stderr)
#   HOT    → HOT      no     present  yes (transition=false)         none
#   HOT    → STABLE   yes    absent   yes (transition=true)          YES (stderr)
#   HOT    → EXTREME  yes    present  yes (transition=true)          YES (stderr)
#   EXTREME → *       yes    updates  yes (transition=true)          YES (stderr)
#
# Sinks (multi):
#   1. JSONL audit       $STATE_DIR/push_divergence.jsonl        (append, every run)
#   2. Marker file       $STATE_DIR/push_divergence.hot          (touch if HOT/EXTREME)
#   3. Cron mail body    stderr, only on TRANSITION              (cron MAILTO)
#
# Forward-points (NOT in this commit per AGENTS.md "Fare PR piccole e mirate"):
#   (a) HOT-persistent reminder (nappa HOT state once every 12h via separate ticker)
#   (b) Multi-branch support (BRANCH=feature/* forward then check per-branch state)
#   (c) Wire-in to start_dashboard_shim.py OR equivalent dashboard aggregator
#   (d) Webhook sink (Slack/Discord) come terzo canale accanto a JSONL + marker
# ═══════════════════════════════════════════════════════════════════════════

set -euo pipefail

GATE_NAME=monitor_push_divergence

# === Constants ===
MON_DEFAULT_THRESHOLD=20
STATE_DIR="${CHRONON3D_MON_STATE_DIR:-$HOME/.chronon3d/monitor}"
STATE_FILE="$STATE_DIR/push_divergence.state"
MARKER_FILE="$STATE_DIR/push_divergence.hot"
JSONL_FILE="$STATE_DIR/push_divergence.jsonl"
GATE_OUT="$STATE_DIR/gate.out.tmp"
GATE_ERR="$STATE_DIR/gate.err.tmp"

REMOTE="${1:-origin}"
BRANCH="${2:-main}"

# === Sanity (Cat-2 internal error → exit 2) ===
REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || { echo "GATE_FAIL_INTERNAL: ${GATE_NAME}: not in a git repo" >&2; exit 2; })"
cd "$REPO_ROOT"

mkdir -p "$STATE_DIR"

# === Load previous state (idempotent: missing → STABLE) ===
PREV_STATE="STABLE"
PREV_LOCAL_AHEAD=0
PREV_REMOTE_AHEAD=0
PREV_TRUE_DIVERGENCE="NO"
PREV_TS_TRANSITION=0
if [ -s "$STATE_FILE" ]; then
    # shellcheck source=/dev/null
    . "$STATE_FILE"
fi

# Threshold (closure lineage: ADR-022 §Decision 2, env-var configurable per warden)
THRESHOLD="${CHRONON3D_MON_THRESHOLD:-$MON_DEFAULT_THRESHOLD}"
if ! [[ "$THRESHOLD" =~ ^[0-9]+$ ]] || [ "$THRESHOLD" -lt 1 ]; then
    echo "GATE_FAIL_INTERNAL: ${GATE_NAME}: CHRONON3D_MON_THRESHOLD=$THRESHOLD is not a positive integer" >&2
    exit 2
fi

# === Invoke the canonical gate (Cat-3 reuse) ===
# The gate emits its own [INFO] on stdout + optional [WARN] on stderr.
# Exit code is always 0 (advisory per ADR-022). Non-0 means INTERNAL error
# (e.g. git fetch fail). Per user decision "solo LOCAL_AHEAD" the wrapper
# passes MAX_REMOTE_AHEAD=0 to the inner gate (ADR-022 §Decision 2: "Setting
# either env var to 0 disables that side's warning entirely") so the inner
# gate does NOT compute a REMOTE-side WARN at all — dual-side separation
# is literal at the env boundary, not just in the wrapper's state machine.
CHRONON3D_DIV_WINDOW_MAX_LOCAL_AHEAD="$THRESHOLD" \
CHRONON3D_DIV_WINDOW_MAX_REMOTE_AHEAD=0 \
    bash tools/check_push_divergence_window.sh "$REMOTE" "$BRANCH" >"$GATE_OUT" 2>"$GATE_ERR"
GATE_RC=$?
if [ "$GATE_RC" -ne 0 ]; then
    cat "$GATE_ERR" >&2
    echo "GATE_FAIL_INTERNAL: ${GATE_NAME}: tools/check_push_divergence_window.sh exited $GATE_RC" >&2
    rm -f "$GATE_OUT" "$GATE_ERR"
    exit 2
fi
GATE_STDOUT="$(cat "$GATE_OUT")"
GATE_STDERR="$(cat "$GATE_ERR" || true)"
rm -f "$GATE_OUT" "$GATE_ERR"

# === Parse [INFO] line from gate output ===
# Pattern: `[INFO] check_push_divergence_window: LOCAL_AHEAD=N REMOTE_AHEAD=M true_divergence=YES/NO ...`
LOCAL_AHEAD="$(printf '%s\n' "$GATE_STDOUT" | sed -n 's/.*LOCAL_AHEAD=\([0-9][0-9]*\).*/\1/p' | head -n1)"
REMOTE_AHEAD="$(printf '%s\n' "$GATE_STDOUT" | sed -n 's/.*REMOTE_AHEAD=\([0-9][0-9]*\).*/\1/p' | head -n1)"
TRUE_DIV="$(printf '%s\n' "$GATE_STDOUT" | sed -n 's/.*true_divergence=\([A-Z]*\).*/\1/p' | head -n1)"
LOCAL_AHEAD="${LOCAL_AHEAD:-0}"
REMOTE_AHEAD="${REMOTE_AHEAD:-0}"
TRUE_DIV="${TRUE_DIV:-NO}"

# === Decide new state ===
# Per user spec: monitor LOCAL_AHEAD side only (provider-side ahead).
# EXTREME escalation only on TRUE divergence (no ancestor in either direction).
NEW_STATE="STABLE"
if [ "$TRUE_DIV" = "YES" ]; then
    NEW_STATE="EXTREME"
elif [ "$LOCAL_AHEAD" -gt "$THRESHOLD" ]; then
    NEW_STATE="HOT"
fi

# === Detect transition ===
TRANSITION="false"
if [ "$PREV_STATE" != "$NEW_STATE" ]; then
    TRANSITION="true"
fi

NOW_TS="$(date -u +%s)"
NEW_TS_TRANSITION="$PREV_TS_TRANSITION"
if [ "$TRANSITION" = "true" ]; then
    NEW_TS_TRANSITION="$NOW_TS"
fi

# === SINK 1: JSONL audit (always) ===
# Single-line atomic append; portable via printf.
{
    printf '{"ts":"%s","state":"%s","local_ahead":%s,"remote_ahead":%s,"true_divergence":"%s","threshold":%s,"transition":%s,"prev_state":"%s"}\n' \
        "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
        "$NEW_STATE" \
        "$LOCAL_AHEAD" \
        "$REMOTE_AHEAD" \
        "$TRUE_DIV" \
        "$THRESHOLD" \
        "$TRANSITION" \
        "$PREV_STATE"
} >> "$JSONL_FILE"

# === SINK 2: marker file (present iff NEW_STATE in HOT/EXTREME) ===
if [ "$NEW_STATE" = "STABLE" ]; then
    rm -f "$MARKER_FILE"
else
    : > "$MARKER_FILE"
fi

# === SINK 3: stderr notify (only on TRANSITION) ===
if [ "$TRANSITION" = "true" ]; then
    case "$NEW_STATE" in
        STABLE)
            # Recovery: was HOT or EXTREME, now safe.
            echo "[INFO] ${GATE_NAME}: RECOVERY -> STABLE (was ${PREV_STATE}). local=${LOCAL_AHEAD} remote=${REMOTE_AHEAD} threshold=${THRESHOLD}" >&2
            ;;
        HOT)
            echo "[WARN] ${GATE_NAME}: HOT TRANSITION (was ${PREV_STATE}). LOCAL_AHEAD=${LOCAL_AHEAD} exceeds threshold=${THRESHOLD} (REMOTE_AHEAD=${REMOTE_AHEAD}, true_divergence=${TRUE_DIV}). Push soon to drain the queue before the pre-push hard-block at tools/check_main_clean.sh fires." >&2
            ;;
        EXTREME)
            echo "[WARN] ${GATE_NAME}: EXTREME TRANSITION (was ${PREV_STATE}). TRUE divergence detected (LOCAL_AHEAD=${LOCAL_AHEAD}, REMOTE_AHEAD=${REMOTE_AHEAD}). Reconcile with: git fetch ${REMOTE} && git log --oneline '@{u}'..HEAD && git merge --no-ff ${REMOTE}/${BRANCH}  OR  git pull --rebase ${REMOTE} ${BRANCH}." >&2
            ;;
    esac
fi

# === Atomic state file write (write-tmp + mv for crash-safety) ===
STATE_TMP="$(mktemp "${STATE_FILE}.XXXXXX.tmp")"
# Crash-cleanup trap: if mv fails (disk full, perms) under set -e, the
# tempfile would otherwise linger in $STATE_DIR. Trap clears it on EXIT.
trap 'rm -f "$STATE_TMP" 2>/dev/null || true' EXIT
{
    printf 'STATE=%s\n' "$NEW_STATE"
    printf 'LAST_LOCAL_AHEAD=%s\n' "$LOCAL_AHEAD"
    printf 'LAST_REMOTE_AHEAD=%s\n' "$REMOTE_AHEAD"
    printf 'LAST_TRUE_DIVERGENCE=%s\n' "$TRUE_DIV"
    printf 'LAST_RUN_TS=%s\n' "$NOW_TS"
    printf 'TS_TRANSITION=%s\n' "$NEW_TS_TRANSITION"
} > "$STATE_TMP"
mv "$STATE_TMP" "$STATE_FILE"

# === Final summary (Rule #2 INFO-level diagnostic style) ===
# Silent on STABLE→STABLE to avoid cron mail spam every 5 minutes;
# on first run (no prev state) OR transition OUT of STABLE the [INFO] line emits
# to stdout for visual confirmation when invoked interactively.
if [ "$NEW_STATE" = "STABLE" ] && [ "$PREV_STATE" = "STABLE" ]; then
    # steady-state: silent (cron false-positive prevention per §honesty)
    :
else
    echo "[INFO] ${GATE_NAME}: state=${NEW_STATE} local=${LOCAL_AHEAD}/${THRESHOLD} remote=${REMOTE_AHEAD} true_divergence=${TRUE_DIV} transition=${TRANSITION}"
fi

exit 0
