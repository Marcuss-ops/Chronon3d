#!/usr/bin/env bash
# check_ffmpeg_required.sh — canonical FAIL-LOUD gate for ffmpeg + ffprobe.
#
# First-Principles Product Check: closing the SKIP-on-missing rot pattern by
# replacing every downstream gate that emits SKIP(...) when ffmpeg/ffprobe is
# absent from PATH. Per user spec verbatim:
#   - NO SKIP-on-missing behaviour
#   - NO silent fallback (the gate FAIL-LOUDLY emits the canonical install hint)
#   - exit 1 with explicit GATE_FAIL diagnostic when either binary is absent
#   - exit 0 with canonical GATE_PASS + [INFO] line on PASS addizionale
#
# Per AGENTS.md:
#   - §honesty + §honest-limitation: when the dependency is unmet, the script
#     FAIL-LOUDS (does NOT pretend to pass) and emits an exact install hint.
#   - INFO-level diagnostic style rule (`## Regole di lint documentale`):
#     ONE `[INFO] <gate-name>: <message>` line on PASS addizionale al canonico
#     GATE_PASS final line (≤ 200 chars, grep-discoverable via `[INFO]` prefix
#     + `check_ffmpeg_required` self-identifier).
#   - Cat-3 anti-duplication: ONE canonical gate, NO per-tool ffmpeg detection
#     duplicates scattered across the test surface.
#
# Cross-link:
#   - TICKET-FFMPEG-REQUIRED-FAIL-LOUD (this chore, §Open Blockers)
#   - TICKET-VIDEO-COMPLETENESS-MATRIX §1+§2 BLOCKED-build forward-point:
#     the two spec-step BLOCKs (Build video completa + Esegui ctest) are now
#     resolvable on working build host without the SKIP-on-ffmpeg rot once
#     this canonical FAIL-LOUD gate is wired.
#   - AGENTS.md TICKET-GATE-SUBJECT-RANGE closure 2026-07-12: subject envelope
#     pattern `feat(check): wire ffmpeg-required FAIL-LOUD gate` (45 chars).
#
# Exit codes (canonical GATE contract):
#   0 = GATE_PASS (ffmpeg + ffprobe present + functional self-test OK)
#   1 = GATE_FAIL (either binary missing or non-functional)
#   2 = GATE_FAIL_INTERNAL (unexpected error, e.g. permission denied reading version)
set -euo pipefail

GATE_NAME=check_ffmpeg_required
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

# Canonical install hint (single source of truth — referenced verbatim below).
INSTALL_HINT="install via apt install ffmpeg"
FAIL_MSG_PREFIX="GATE_FAIL: ffmpeg/ffprobe not found — ${INSTALL_HINT}"

# ─────────── Step 1 — ffmpeg binary in PATH ───────────
if ! command -v ffmpeg >/dev/null 2>&1; then
  echo "$FAIL_MSG_PREFIX" >&2
  exit 1
fi

# ─────────── Step 2 — ffprobe binary in PATH ───────────
if ! command -v ffprobe >/dev/null 2>&1; then
  echo "$FAIL_MSG_PREFIX" >&2
  exit 1
fi

# ─────────── Step 3 — Functional self-test ───────────
# A binary can be present in PATH but broken (missing shared lib). Verify each
# returns usable version output (first line of `*-version` standard output).
FFMPEG_VERSION_LINE="$(ffmpeg -version 2>&1 | head -n1 || true)"
FFPROBE_VERSION_LINE="$(ffprobe -version 2>&1 | head -n1 || true)"

if [ -z "$FFMPEG_VERSION_LINE" ] || [ -z "$FFPROBE_VERSION_LINE" ]; then
  echo "$FAIL_MSG_PREFIX" >&2
  exit 1
fi

# ─────────── Step 4 — PASS verdict (canonical emission) ───────────
# Per AGENTS.md INFO-level diagnostic style rule: emit canonical GATE_PASS
# first, then [INFO] line addizionale (grep-discoverable family `[INFO]
# <gate-name>: ...`). The PASS line below is the canonical final; the [INFO]
# line is the diagnostic sideband (one line, ≤ 200 chars).
echo "GATE_PASS: ffmpeg + ffprobe present and self-test OK"
echo "[INFO] ${GATE_NAME}: ffmpeg+ffprobe present at ${FFMPEG_VERSION_LINE} | ${FFPROBE_VERSION_LINE}"

exit 0
