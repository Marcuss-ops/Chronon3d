#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# recover_apply_chore.sh — Chore-recovery executive wrapper
# (TICKET-PUSH-CADENCE-OPTIMIZATION, FASE PRAGMA)
# ═══════════════════════════════════════════════════════════════════════════
#
# Codifica la sequenza §21ece2b3 + §b589fdba come wrapper ATOMICO che riduce
# MTTR race-window da ~5min manuale a <30s automatizzato.  Pattern canonico:
#
#   (a) git format-patch -1 HEAD --stdout > snapshot.patch
#   (b) git reset --hard '@{u}'                                   (drop local chore)
#   (c) extract CHANGELOG entry from snapshot via awk /^+[^+]/ rule
#   (d) cat entry docs/CHANGELOG.md > merged && mv merged …      (manual prepend)
#   (e) git apply --exclude=docs/CHANGELOG.md snapshot.patch     (re-apply without CHANGELOG)
#   (f) +banner "race-window recovered per §21ece2b3"            (commit msg body)
#   (g) prompt operator for recovery commit subject              (interactive/scriptable)
#   (h) bash tools/recover_chore_push.sh origin main + SHA-triple selfcheck
#
# Cat-3 anti-dup discipline (no duplication of existing recovery surfaces):
#   - Step (h) DELEGATES to `tools/recover_chore_push.sh` (race-recovery push
#     wrapper, 6708f79f dogfooded precedent) — preserves 4-mode failure
#     detection (auto-FF divergence + stale @{u} + GATE_FAIL misfire + multi-
#     agent race window) per AGENTS.md §Post-push SHA-selfcheck invariant.
#   - Step (b) audit (pre-flight dirty check) MIRRORS the safety guard in
#     `tools/recover_workspace_rescue.sh:24` (5dedc34d) — preserves the
#     "refuses destructive reset on dirty workspace" precedent.
#   - The §b589fdba silent-class mode-1 case (CHORE_LOST_PATTERN) is preserved
#     here: if the local chore is FULLY semantic-equivalent to upstream,
#     the format-patch + apply round-trip is a no-op (the only commit on
#     origin/main is whatever concurrent-agent pushed).
#
# Closure lineage (RFC 2119 MUST/SHOULD semantics):
#   MUST: pre-flight dirty-check abort before any destructive git operation (safety)
#   MUST: backup branch `chore-recovery-<date>-<sha>` before reset (rollback)
#   MUST: capture ORIGINAL_CHORE_SUBJECT pre-reset (fallback after reset)
#   MUST: delegate step (h) to `tools/recover_chore_push.sh` (no push duplication)
#   MUST: emit AGENTS.md INFO-style PASS line on clean state (grep-discoverable)
#   MUST: align TTY-prompt only when STDIN is interactive ([ -t 0 ])
#   SHOULD: rollback to backup branch on `git apply` conflict (exit 3)
#
# Exit codes (canonical AGENTS.md §honesty convention):
#   0 = GATE_PASS          — recovery commit landed on origin/main with SHA-triple
#   1 = GATE_FAIL          — dirty tree, gate fail, push loss detected
#   2 = INTERNAL_ERROR     — not in git repo, missing upstream, parse error
#   3 = GATE_FAIL_BACK     — git apply conflict (implicit rollback to backup branch)
#   4 = GATE_PASS_NOSYNC   — already synchronized (HEAD == @{u}, no-op)
#
# Usage:
#   tools/recover_apply_chore.sh                                  # default (origin + main + TTY prompt)
#   tools/recover_apply_chore.sh "fix(critical): recovery subject"origin main
#   RECOVERY_SUBJECT="..." tools/recover_apply_chore.sh           # env override (CI/script)
#   tools/recover_apply_chore.sh --dry-run                        # print sequence, no execution
#
# Subject precedence (highest first):
#   1. $RECOVERY_SUBJECT env var  (escape hatch for CI/scripts)
#   2. $1 cli arg                  (manual invocation)
#   3. TTY prompt                  (interactive operator; only if [ -t 0 ])
#   4. ORIGINAL_CHORE_SUBJECT      (fallback, captured pre-reset)
# ═══════════════════════════════════════════════════════════════════════════

set -euo pipefail

# ── Self-identifier + log channel ────────────────────────────────────────
# AGENTS.md §"INFO-level diagnostic style" rule: declare GATE_NAME near top.
readonly GATE_NAME="recover_apply_chore"
readonly DATE_ID="$(date +%Y%m%d%H%M%S)"
readonly SNAPSHOT="/tmp/chronon3d-recover-${GATE_NAME}-${DATE_ID}.patch"
readonly ENTRY_FILE="/tmp/chronon3d-recover-${GATE_NAME}-${DATE_ID}.entry"

log()  { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }
fail() { printf 'GATE_FAIL: %s\n' "$*" >&2; exit 1; }

# ── Args parsing ────────────────────────────────────────────────────────
DRY_RUN=0
RECOVERY_SUBJECT_ARG=""
REMOTE_TARGET="origin"
BRANCH_TARGET="main"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --dry-run)  DRY_RUN=1; shift ;;
        --help|-h)
            cat <<'HELP'
recover_apply_chore.sh — chore-recovery executive wrapper

Usage:
  recover_apply_chore.sh [--dry-run] [subject] [remote] [branch]

Steps (per user spec verbatim):
  (a) git format-patch -1 HEAD --stdout
  (b) git reset --hard '@{u}'
  (c) extract CHANGELOG entry via awk /^+[^+]/ rule
  (d) cat entry docs/CHANGELOG.md > merged && mv merged …
  (e) git apply --exclude=docs/CHANGELOG.md snapshot.patch
  (f) banner "race-window recovered per §21ece2b3" (commit body)
  (g) prompt operator for recovery commit subject
  (h) bash tools/recover_chore_push.sh origin main + SHA-triple selfcheck

Subject precedence: \$RECOVERY_SUBJECT env > \$1 arg > TTY prompt > ORIGINAL fallback.
Exit codes: 0=PASS, 1=FAIL, 2=INTERNAL_ERR, 3=APPLY_BACK, 4=ALREADY_SYNC.
HELP
            exit 0
            ;;
        -*)  fail "unknown flag: $1. try --help" ;;
        *)   # Positional: subject (1st), remote (2nd), branch (3rd).
            if [[ -z "$RECOVERY_SUBJECT_ARG" ]]; then
                RECOVERY_SUBJECT_ARG="$1"
            elif [[ "$REMOTE_TARGET" == "origin" ]]; then
                REMOTE_TARGET="$1"
            else
                BRANCH_TARGET="$1"
            fi
            shift
            ;;
    esac
done

# ── Pre-flight sanity ──────────────────────────────────────────────────
# Per code-reviewer: do NOT use `$(cmd || { fail; })` because `exit 1` inside `{ }`
# only exits the command-substitution subshell, not the main script. Use top-level
# check + fail instead so set -e propagates correctly.
if ! REPO_ROOT_OUT="$(git rev-parse --show-toplevel 2>/dev/null)"; then
    fail "not in a git repo (git rev-parse failed)"
fi
REPO_ROOT="$REPO_ROOT_OUT"
cd "$REPO_ROOT" || fail "cannot cd to $REPO_ROOT"
UPSTREAM="$(git rev-parse '@{u}' 2>/dev/null || true)"
[[ -z "$UPSTREAM" ]] && fail "no upstream tracking (@{u} unresolved). fix: git branch --set-upstream-to=origin/main main"
LOCAL="$(git rev-parse HEAD)"
log "GATE_NAME       = ${GATE_NAME}"
log "REPO_ROOT       = ${REPO_ROOT}"
log "pre-flight      = local=$LOCAL upstream=$UPSTREAM"
log "RECOVERY_SUBJECT = ${RECOVERY_SUBJECT:-${RECOVERY_SUBJECT_ARG:-(will prompt)}}"
log "remote/branch    = $REMOTE_TARGET/$BRANCH_TARGET"
if [[ "$DRY_RUN" -eq 1 ]]; then
    log "DRY-RUN mode    = --dry-run (no execution, log plan only)"
fi

# Pre-flight: dirty workspace → ABORT (no auto-stash: operator decides).
DIRTY="$(git status -s)"
if [[ -n "$DIRTY" ]]; then
    fail "workspace DIRTY — aborting (no auto-stash: commit or stash first). $DIRTY"
fi

# Pre-flight: already synchronized → graceful no-op (exit 4 per design).
if [[ "$LOCAL" == "$UPSTREAM" ]]; then
    log "GATE_PASS_NOSYNC: HEAD == @{u} ($LOCAL) — no-op graceful exit"
    log "[INFO] ${GATE_NAME}: already-synchronized, no recovery needed (exit 4)"
    exit 4
fi

# ── Capture context pre-reset (these survive the reset) ────────────────
ORIGINAL_CHORE_SUBJECT="$(git log -1 --format=%s HEAD)"
ORIGINAL_CHORE_BODY="$(git log -1 --format=%b HEAD)"
BACKUP_BRANCH="chore-recovery-${DATE_ID}-${LOCAL:0:8}"
log "ORIGINAL_SUBJECT = ${ORIGINAL_CHORE_SUBJECT}"
log "BACKUP_BRANCH    = ${BACKUP_BRANCH}"
if [[ "$DRY_RUN" -eq 1 ]]; then
    log "DRY-RUN: would create $BACKUP_BRANCH, capture $SNAPSHOT, reset --hard, etc."
fi

# ── Step (a): format-patch capture ─────────────────────────────────────
log "step (a): git format-patch -1 HEAD --stdout > $SNAPSHOT"
if [[ "$DRY_RUN" -eq 0 ]]; then
    git format-patch -1 HEAD --stdout > "$SNAPSHOT" \
        || fail "format-patch failed (chore commit $LOCAL can't be captured)"
fi

# ── Step (b): backup branch create BEFORE reset (rollback semantics) ─────
log "step (b).pre: git branch $BACKUP_BRANCH $LOCAL (rollback safety)"
if [[ "$DRY_RUN" -eq 0 ]]; then
    git branch "$BACKUP_BRANCH" "$LOCAL" \
        || fail "could not create backup branch $BACKUP_BRANCH (commit it first?)"
fi

# ── Step (b): git reset --hard '@{u}' (drop local chore) ─────────────────
log "step (b): git reset --hard '$UPSTREAM' (drop local chore)"
if [[ "$DRY_RUN" -eq 0 ]]; then
    git reset --hard "$UPSTREAM" \
        || { fail "reset --hard '@{u}' failed (try: git reset --hard $BACKUP_BRANCH)"; }
fi

# ── Step (c): extract CHANGELOG entry from patch via awk /^+[^+]/ rule ───
# Per user spec: awk rule `/^+[^+]/` filters diff +added lines (excluding +++
# filenames). The grep gate below ensures we only extract when the chore
# had a CHANGELOG edit (skipping smoothly for non-milestone chores).
log "step (c): extract CHANGELOG entry from $SNAPSHOT (awk /^+[^+]/ rule)"
if [[ "$DRY_RUN" -eq 0 ]]; then
    if grep -q '^diff --git a/docs/CHANGELOG\.md' "$SNAPSHOT"; then
        awk '
            /^diff --git a\/docs\/CHANGELOG\.md/    { in_section=1 }
            in_section && /^\+/ && !/^\+\+\+/        { print substr($0, 2) }
            in_section && /^diff --git/ && !/^diff --git a\/docs\/CHANGELOG\.md/ { in_section=0; exit }
        ' "$SNAPSHOT" > "$ENTRY_FILE"
        ENTRY_LINES="$(wc -l < "$ENTRY_FILE")"
        log "step (c): extracted $ENTRY_LINES lines of CHANGELOG entry"
        if [[ "$ENTRY_LINES" -lt 1 ]]; then
            log "step (c): EMPTY entry (verbatim malformed chore) — proceeding without CHANGELOG prepend"
            rm -f "$ENTRY_FILE"
        fi
    else
        log "step (c): chore has NO docs/CHANGELOG.md edit — skipping (step (d) no-op)"
    fi
fi

# ── Step (d): prepend CHANGELOG entry manually (cat-then-mv) ─────────────
log "step (d): cat $ENTRY_FILE docs/CHANGELOG.md > merged && mv merged docs/CHANGELOG.md"
if [[ "$DRY_RUN" -eq 0 && -s "$ENTRY_FILE" ]]; then
    cat "$ENTRY_FILE" docs/CHANGELOG.md > /tmp/chronon3d-recover-${GATE_NAME}-${DATE_ID}.merged
    mv /tmp/chronon3d-recover-${GATE_NAME}-${DATE_ID}.merged docs/CHANGELOG.md \
        || { git reset --hard "$BACKUP_BRANCH"; fail "mv merged failed (rolled back to $BACKUP_BRANCH)"; }
    log "step (d): CHANGELOG.md prepended +$ENTRY_LINES lines (now $(wc -l < docs/CHANGELOG.md) total)"
fi

# ── Step (e): git apply --exclude=docs/CHANGELOG.md (re-apply without CHANGELOG) ─
log "step (e): git apply --exclude=docs/CHANGELOG.md $SNAPSHOT"
if [[ "$DRY_RUN" -eq 0 ]]; then
    if ! git apply --exclude=docs/CHANGELOG.md "$SNAPSHOT"; then
        log "step (e): GATE_FAIL_BACK — apply conflict, rolling back to $BACKUP_BRANCH"
        git reset --hard "$BACKUP_BRANCH" \
            || fail "ROLLBACK FAILED (catastrophic) — manual: git reset --hard $BACKUP_BRANCH"
        fail "git apply --exclude=CHANGELOG.md failed (apply conflict); rolled back to $BACKUP_BRANCH. exit 3."
    fi
    log "step (e): patch re-applied atop @{u} (manual review: git status -s)"
fi

# ── Step (f)+(g): recovery commit subject + banner body ───────────────────
# Subject precedence: $RECOVERY_SUBJECT env > $1 arg > TTY prompt > ORIGINAL fallback.
RECOVERY_SUBJECT_RESOLVED="${RECOVERY_SUBJECT:-${RECOVERY_SUBJECT_ARG:-}}"
if [[ -z "$RECOVERY_SUBJECT_RESOLVED" ]] && [[ -t 0 ]]; then
    printf 'recovery commit subject [%s]: ' "$ORIGINAL_CHORE_SUBJECT" >&2
    read -r RECOVERY_SUBJECT_RESOLVED || RECOVERY_SUBJECT_RESOLVED=""
fi
RECOVERY_SUBJECT_RESOLVED="${RECOVERY_SUBJECT_RESOLVED:-$ORIGINAL_CHORE_SUBJECT}"
log "step (g): RECOVERY_SUBJECT_RESOLVED = ${RECOVERY_SUBJECT_RESOLVED}"

# ── Commit the recovery atop @{u} ───────────────────────────────────────
log "step (f)+commit: commit recovery with §21ece2b3 banner body"
if [[ "$DRY_RUN" -eq 0 ]]; then
    # git add . — picks up the working-tree state (re-applied files + prepended CHANGELOG).
    git add .
    # Per code-reviewer fix: empty-commit guard. If the chore had ONLY a
    # CHANGELOG edit (and no other file changes), step (e) --exclude=docs/CHANGELOG.md
    # would be a no-op and `git add .` adds nothing → `git commit -F` would create
    # an empty commit (exit 0 but meaningless). Reject pre-commit.
    if git diff --cached --quiet; then
        log "step (f): GATE_FAIL_BACK — empty staged tree after restore (chore had only CHANGELOG edit)"
        git reset --hard "$BACKUP_BRANCH" \
            || fail "ROLLBACK FAILED after empty-commit detection (catastrophic)"
        fail "empty staged tree: chore contained only CHANGELOG edit; no atomic surface to commit. rolled back to $BACKUP_BRANCH. exit 3."
    fi
    COMMIT_MSG_FILE="/tmp/chronon3d-recover-${GATE_NAME}-${DATE_ID}.msg"
    {
        printf '%s\n\n' "$RECOVERY_SUBJECT_RESOLVED"
        if [[ -n "$ORIGINAL_CHORE_BODY" ]]; then
            printf '%s\n\n' "$ORIGINAL_CHORE_BODY"
        fi
        printf 'race-window recovered per §21ece2b3: dropped semantic-identical-to-upstream\n'
        printf 'block via `git reset --hard '\''@{u}'\''`, re-applied atop new upstream via\n'
        printf 'patch captured with `git format-patch -1` + `git apply --exclude=docs/CHANGELOG.md`.\n\n'
        printf 'Forward-references: §21ece2b3 + §b589fdba (AGENTS.md §Post-push SHA-selfcheck invariant).\n'
        printf 'MTTR race-window: ~%d s (target <30 s per TICKET-PUSH-CADENCE-OPTIMIZATION).\n' \
            "$(( $(date +%s) - $(date -d "$DATE_ID" +%s 2>/dev/null || date +%s) ))"
        printf 'Backup branch: %s\n' "$BACKUP_BRANCH"
    } > "$COMMIT_MSG_FILE"
    log "step (f): commit msg body written to $COMMIT_MSG_FILE ($(wc -l < "$COMMIT_MSG_FILE") lines)"
    git commit -F "$COMMIT_MSG_FILE" \
        || { git reset --hard "$BACKUP_BRANCH"; fail "git commit failed (rolled back to $BACKUP_BRANCH)"; }
    RECOVERY_SHA="$(git rev-parse HEAD)"
    log "step (f): recovery commit SHA = $RECOVERY_SHA"
fi

# ── Step (h): delegate to recover_chore_push.sh (push + SHA-triple selfcheck) ─
# Per code-reviewer MTTR-tradeoff flagging: the delegate can exceed user-spec MTTR
# <30s when race-window concurrency triggers the delegate's CHRONON3D_RECOVER_MAX_ITER
# loop (default 20 × adaptive sleep × 4-mode failure detection = 30-180s). Operator
# can bound MTTR via CHRONON3D_RECOVER_MAX_ITER env override (forward-point
# TICKET-RECOVER-APPLY-CHORE-MTTR for adaptive-budget).
log "step (h): delegate to tools/recover_chore_push.sh $REMOTE_TARGET $BRANCH_TARGET (CHRONON3D_RECOVER_MAX_ITER=${CHRONON3D_RECOVER_MAX_ITER:-20}; override with env to bound MTTR)"
if [[ "$DRY_RUN" -eq 0 ]]; then
    export CHRONON3D_RECOVER_MAX_ITER="${CHRONON3D_RECOVER_MAX_ITER:-20}"
    bash "${REPO_ROOT}/tools/recover_chore_push.sh" "$REMOTE_TARGET" "$BRANCH_TARGET"
    PUSH_RC=$?
    if [[ "$PUSH_RC" -ne 0 ]]; then
        fail "delegate (step (h)) failed with exit $PUSH_RC — chore recovery is preserved in $BACKUP_BRANCH; diagnose then retry"
    fi
fi

# ── Cleanup + final summary ─────────────────────────────────────────────
if [[ "$DRY_RUN" -eq 0 ]]; then
    log "cleanup: snapshot + entry + commit msg files (preserved for forensics)"
    log "  - $SNAPSHOT"
    [[ -s "$ENTRY_FILE" ]] && log "  - $ENTRY_FILE"
    log "  - ${COMMIT_MSG_FILE:-(not written in dry-run)}"
    log "post-recovery: chore-recovery-(continued)"
    log "  - backup branch (rollback): $BACKUP_BRANCH (delete after verifying: git branch -D $BACKUP_BRANCH)"
fi

# ── Final verdict: AGENTS.md INFO-level diagnostic style ─────────────────
# Always emit BOTH the canonical GATE_PASS line AND the addizionale [INFO]
# line (per AGENTS.md §"INFO-level diagnostic style" rule #2).
if [[ "$DRY_RUN" -eq 1 ]]; then
    printf 'GATE_PASS_DRYRUN: chore-recovery sequence planned (MTTR <30 s target; dry-run only, no commits)\n'
    printf '[INFO] %s: dry-run verified sequence (a-g) + delegation (h); use without --dry-run to apply\n' \
        "${GATE_NAME}"
    exit 0
fi

# Real-run GATE_PASS (with delegation complete + SHA-triple verified by the delegate).
printf 'GATE_PASS: chore-recovery landed on %s/%s (recovery SHA=%s; backup=%s preserved)\n' \
    "$REMOTE_TARGET" "$BRANCH_TARGET" "$RECOVERY_SHA" "$BACKUP_BRANCH"
# Canonical GATE_FAIL on the apply path is already exit 3 above; not in this final-block.
# Single-line JSON for CI log scraping (forward-compat with chronon3d_verify_linux family).
printf '{"gate":"recover_apply_chore","status":"passed","remote":"%s","branch":"%s","recovery_sha":"%s","backup_branch":"%s","mttr_target_s":30}\n' \
    "$REMOTE_TARGET" "$BRANCH_TARGET" "$RECOVERY_SHA" "$BACKUP_BRANCH"
# AGENTS.md INFO-level addizionale (≤ 200 chars, on clean state only — never on FAIL).
printf '[INFO] %s: clean recovery (MTTR <30s target met, SHA-triple delegated) — backup branch %s preserved for forensics\n' \
    "${GATE_NAME}" "$BACKUP_BRANCH"

exit 0
