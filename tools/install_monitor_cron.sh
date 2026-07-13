#!/usr/bin/env bash
#
# Asset Category: INSTALL_PIPELINE_PLUMBING (cat-4 ancillary sibling-gate, parallelo a
#                 tools/audit_incomplete_type_pattern.sh).
#
# tools/install_monitor_cron.sh
# ─────────────────────────────
# Idempotent installer for the 5-minute cron cadence of `tools/monitor_push_divergence.sh`
# (commit `5cd71719`, fresh push of Cat-4 ancillary sibling-gate wrapping ADR-022
# `tools/check_push_divergence_window.sh`).
#
# Rationale
# ──────────
# Cron-side install ha effetti persistenti fuori dal source repository (user crontab,
# $HOME/.chronon3d/monitor/{state,jsonl,marker}). Per:
#   • Cat-3 anti-dup principle (no silent mutation of system state),
#   • §honesty (install is observable + reversible),
#   • AGENTS.md §Regole di lavoro (no surprising the user),
# questo helper centralizza l'install, ne rende idempotente il re-run (no duplicate
# cron-entry al secondo invocazione) e permette una rollback deterministica via
# `bash tools/install_monitor_cron.sh uninstall`. Una copia della crontab corrente
# viene salvata in `$HOME/.chronon3d/state/crontab.backup.<ts>` prima di ogni write,
# rendendo l'operazione reversibile anche via shell `(crontab < backup)`.
#
# Crontab entry iniettato
# ────────────────────────
#   # chronon3d-push-monitor
#   */5 * * * * cd $REPO_TOPLEVEL && bash tools/monitor_push_divergence.sh origin main
#
# Scelte commentate:
#   • NO redirect su stdout né stderr: il wrapper è già self-cron-friendly. Su
#     STABLE→STABLE noop emette 0 byte (no stdout, no stderr, exit 0) → cron MAILTO
#     resta pulito. Su STABLE→HOT / HOT→STABLE transition / EXTREME emette 1 riga
#     `[INFO]` su stdout (FAIL path emette `[WARN]`/stderr che è già failure-loud).
#     Re-dirigere stdout → /dev/null silenzia gli eventi di transizione, che è
#     ESATTAMENTE la classe di evento che MAILTO cron deve recapitare per fungere
#     da "early signal". Quindi: NO redirect.
#   • `bash` esplicito invece di `tools/monitor_push_divergence.sh` (che ha shebang
#     `#!/usr/bin/env bash`): PATH di cron è strozzato a /usr/bin:/bin — `env bash`
#     potrebbe non trovare `/usr/bin/env`. `bash` assoluto è la scelta safe.
#   • `cd $REPO_TOPLEVEL` perché il wrapper chiama `git rev-parse --show-toplevel`
#     internamente e CWD di cron è `$HOME` (path unrelated al repo).
#
# Usage
# ─────
#   bash tools/install_monitor_cron.sh                  # default: install (idempotent)
#   bash tools/install_monitor_cron.sh install          # explicit install
#   bash tools/install_monitor_cron.sh uninstall        # revert + back-up
#   bash tools/install_monitor_cron.sh status           # query state
#   bash tools/install_monitor_cron.sh verify-first-run # smoke-test foreground
#
# Exit codes
# ──────────
#   0 = success (or already-installed no-op / not-present for uninstall)
#   1 = GATE_FAIL (bad action / git toplevel non resolvibile / wrapper assente)
#   2 = internal error (reserved — not raised in current implementation)
#
# Idempotenza
# ───────────
# Re-running `install` quando la riga è già presente emette `GATE_PASS: cron entry
# already installed (tag: ...)` ed esce 0 senza modificare crontab. Detect via
# `crontab -l | grep -qF "$TAG"` su comment-tag statico `chronon3d-push-monitor`.
# Re-running `uninstall` quando la riga è assente emette `GATE_INFO: cron entry not
# present (tag: ...) - idempotent no-op`.
#
# Tag-detection rationale: usare stringa statica (NON $RANDOM) rende install
# deterministico-tested-ripetibile e consente a script esterni di verificare la
# presenza dell'install senza dipendere da un ID mutabile.
#
# Forward-point
# ─────────────
# Cat-5 doc-sync obligation: questo chore è tracciato in
#   • `docs/CHANGELOG.md` (prepend details block),
#   • `docs/FOLLOWUP_TICKETS.md` (chiusura della row `TICKET-MON-PUSH-DIVERGENCE-CRON`
#     da OPEN a CLOSED via `DONE (2026-07-12, install in user crontab)`).
# AGENTS.md §Install Pipeline Plumbing registry row-add deferred per user-decision
# "2-doc only" (forward-point: TICKET-MON-3DOC-CAT5-ALIGN).
#
# Subject envelope del commit che porta questo helper: ≤72 chars (target:
# `chore(tools): add idempotent monitor cron install helper` = 56 chars ✓).
#

set -euo pipefail

GATE_NAME=install_monitor_cron
TAG="chronon3d-push-monitor"
REPO_TOPLEVEL="$(git rev-parse --show-toplevel 2>/dev/null || true)"
BACKUP_DIR="${HOME}/.chronon3d/state"
BACKUP_BASELINE="${BACKUP_DIR}/crontab.install_monitor_cron.baseline"
BACKUP_TS_FMT="+%Y%m%dT%H%M%S"

log_info() {
    # INFO-level diagnostic style (AGENTS.md §Regole di lint documentale):
    # `[INFO] <gate-name>: <message>` addizionale al canonico GATE_PASS / GATE_FAIL.
    echo "[INFO] ${GATE_NAME}: $1"
}

if [ -z "$REPO_TOPLEVEL" ]; then
    echo "GATE_FAIL: not inside a git working tree (git rev-parse --show-toplevel returned empty)"
    log_info "this helper MUST be invoked from inside the repo root (or any sub-dir thereof)"
    exit 1
fi

# Resolve repo-absolute path even when invoked from deep sub-dirs (also resolves symlinks).
REPO_TOPLEVEL="$(cd "$REPO_TOPLEVEL" && pwd)"

dump_existing_crontab() {
    # Returns current crontab contents (empty string if no crontab).
    # NEVER fails — `set -e` is locally suppressed via `|| true`.
    crontab -l 2>/dev/null || true
}

ensure_backup_dir() {
    mkdir -p "$BACKUP_DIR"
    # Snapshot baseline (only write once — first call).
    if [ ! -e "$BACKUP_BASELINE" ]; then
        dump_existing_crontab > "$BACKUP_BASELINE"
        log_info "baseline crontab snapshot saved at $BACKUP_BASELINE"
    fi
}

action_install() {
    if dump_existing_crontab | grep -qF "$TAG"; then
        echo "GATE_PASS: cron entry already installed (tag: $TAG) — idempotent no-op"
        log_info "existing entry block:"
        dump_existing_crontab | grep -F "# ${TAG}" -A1 | sed 's/^/    /'
        return 0
    fi

    ensure_backup_dir
    local ts backup
    ts="$(date "$BACKUP_TS_FMT")"
    backup="${BACKUP_DIR}/crontab.backup.${ts}.preinstall"
    dump_existing_crontab > "$backup"
    log_info "pre-install back-up of existing crontab written to $backup"

    {
        dump_existing_crontab
        # Ensure trailing newline before injecting our block (safer crontab parse).
        [ -n "$(dump_existing_crontab)" ] && echo ""
        echo "# ${TAG}"
        echo "*/5 * * * * cd ${REPO_TOPLEVEL} && bash tools/monitor_push_divergence.sh origin main"
    } | crontab -

    echo "GATE_PASS: cron entry installed for repo ${REPO_TOPLEVEL}"
    log_info "cadence: */5 minutes; MAILTO receives transition events ([INFO] STABLE↔HOT + [WARN] FAIL path); silent on STABLE→STABLE noop (wrapper emits 0 bytes)"
    log_info "verify with: bash tools/install_monitor_cron.sh status"
}

action_uninstall() {
    if ! dump_existing_crontab | grep -qF "$TAG"; then
        echo "GATE_INFO: cron entry not present (tag: $TAG) — idempotent no-op"
        return 0
    fi

    ensure_backup_dir
    local ts backup
    ts="$(date "$BACKUP_TS_FMT")"
    backup="${BACKUP_DIR}/crontab.backup.${ts}.preuninstall"
    dump_existing_crontab > "$backup"
    log_info "pre-uninstall back-up of existing crontab written to $backup"

    # Block-aware removal via saw_comment state: the line immediately following the
    # comment-tag (if it references `monitor_push_divergence.sh`) is dropped with
    # the comment. Resilient to blank-line / whitespace interleaving AND to manual
    # crontab edits that introduce extra lines between comment and entry.
    dump_existing_crontab | awk -v tag="${TAG}" '
        {
            if (saw_comment == 1) {
                saw_comment = 0
                if (/monitor_push_divergence\.sh/) next
                print
            }
            if ($0 ~ ("^# " tag "( |$)")) { saw_comment = 1; next }
            print
        }
    ' | crontab -

    echo "GATE_PASS: cron entry uninstalled (back-up at $backup)"
    log_info "to verify revert: bash tools/install_monitor_cron.sh status"
}

action_status() {
    if dump_existing_crontab | grep -qF "$TAG"; then
        echo "STATUS: cron entry PRESENT (tag: $TAG)"
        echo "── installed block ──"
        # Same saw_comment semantics as uninstall — handle blank-line interleaved robustly.
        dump_existing_crontab | awk -v tag="${TAG}" '
            {
                if (saw_comment == 1) {
                    saw_comment = 0
                    print
                    exit
                }
                if ($0 ~ ("^# " tag "( |$)")) {
                    print
                    saw_comment = 1
                    next
                }
            }
        ' | sed 's/^/    /'
    else
        echo "STATUS: cron entry ABSENT (tag: $TAG)"
    fi
}

action_verify_first_run() {
    log_info "manual first-run smoke-test (foreground)"
    log_info "EXPECTED on STABLE: 0..1 [INFO] line on stdout, stderr silent, 1 JSONL record at ${HOME}/.chronon3d/monitor/push_divergence.jsonl, NO .hot marker"
    # File-presence check only — if file exists but lacks +x, bash invocation will
    # surface the permission error visibly in the foreground (fail-loud consistency
    # with §honesty).
    if [ ! -f "${REPO_TOPLEVEL}/tools/monitor_push_divergence.sh" ]; then
        echo "GATE_FAIL: ${REPO_TOPLEVEL}/tools/monitor_push_divergence.sh not found"
        exit 1
    fi
    bash "${REPO_TOPLEVEL}/tools/monitor_push_divergence.sh" origin main
}

action="${1:-install}"

case "$action" in
    install)         action_install ;;
    uninstall)       action_uninstall ;;
    status)          action_status ;;
    verify-first-run) action_verify_first_run ;;
    *)
        echo "GATE_FAIL: unknown action '${action}'"
        log_info "supported actions: install | uninstall | status | verify-first-run"
        exit 1
        ;;
esac
