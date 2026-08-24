#!/usr/bin/env bash
set -euo pipefail

# Run a command and preserve structured diagnostics for post-mortem analysis.
# Usage: tools/capture_crash_artifact.sh OUT_DIR -- command args...

out="${1:?output directory required}"
shift
[[ "${1:-}" == "--" ]] || { echo "usage: $0 OUT_DIR -- command args..." >&2; exit 2; }
shift
[[ $# -gt 0 ]] || { echo "command required" >&2; exit 2; }
mkdir -p "$out"
printf '%s\n' "$*" > "$out/command.txt"
{
    date -u +%Y-%m-%dT%H:%M:%SZ
    uname -a
    git rev-parse HEAD 2>/dev/null || true
} > "$out/environment.txt"

set +e
"$@" >"$out/stdout.log" 2>"$out/stderr.log"
rc=$?
set -e
printf '{"exit_code":%d,"signal":%d}\n' "$rc" "$((rc >= 128 ? rc - 128 : 0))" > "$out/result.json"

if command -v gdb >/dev/null 2>&1 && (( rc >= 128 )); then
    gdb -batch -ex 'thread apply all bt full' --args "$@" > "$out/backtrace.txt" 2>&1 || true
fi
if (( rc >= 128 )); then
    echo "CRASH_ARTIFACT_FAIL: exit=$rc artifacts=$out" >&2
    exit "$rc"
fi
echo "CRASH_ARTIFACT_PASS: exit=$rc artifacts=$out"
