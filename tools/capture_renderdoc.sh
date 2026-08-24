#!/usr/bin/env bash
set -euo pipefail

# Capture a deterministic Chronon Vulkan run through RenderDoc.
# Usage: tools/capture_renderdoc.sh OUT_DIR -- executable [args...]
#
# The wrapper deliberately does not pretend that a missing RenderDoc install
# is a successful debug run. CHRONON3D_RENDERDOC_CMD can point to a custom
# renderdoccmd binary.

out="${1:?output directory required}"
shift
[[ "${1:-}" == "--" ]] || {
    echo "usage: $0 OUT_DIR -- executable [args...]" >&2
    exit 2
}
shift
[[ $# -gt 0 ]] || { echo "executable required" >&2; exit 2; }

renderdoc_cmd="${CHRONON3D_RENDERDOC_CMD:-renderdoccmd}"
command -v "$renderdoc_cmd" >/dev/null 2>&1 || {
    echo "RENDERDOC_CAPTURE_FAIL: '$renderdoc_cmd' not found" >&2
    exit 2
}

mkdir -p "$out"
capture_file="${CHRONON3D_RENDERDOC_CAPTURE_FILE:-$out/chronon_capture.rdc}"
mkdir -p "$(dirname "$capture_file")"
printf '%s\n' "$*" > "$out/command.txt"

set +e
"$renderdoc_cmd" capture \
    --wait-for-exit \
    --capture-file "$capture_file" \
    "$@" >"$out/renderdoc.stdout.log" 2>"$out/renderdoc.stderr.log"
rc=$?
set -e

python3 - "$out" "$capture_file" "$rc" "$@" <<'PY'
import datetime
import json
import os
import platform
import subprocess
import sys

out, capture, rc = sys.argv[1], sys.argv[2], int(sys.argv[3])
command = sys.argv[4:]
try:
    git_sha = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], stderr=subprocess.DEVNULL, text=True
    ).strip()
except (OSError, subprocess.CalledProcessError):
    git_sha = None

metadata = {
    "schema_version": 1,
    "timestamp_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "git_sha": git_sha,
    "cwd": os.getcwd(),
    "platform": platform.platform(),
    "command": command,
    "renderdoc_command": os.environ.get("CHRONON3D_RENDERDOC_CMD", "renderdoccmd"),
    "capture_file": os.path.abspath(capture),
    "exit_code": rc,
    "capture_bytes": os.path.getsize(capture) if os.path.isfile(capture) else 0,
}
with open(os.path.join(out, "capture.json"), "w", encoding="utf-8") as handle:
    json.dump(metadata, handle, indent=2, sort_keys=True)
    handle.write("\n")
PY

if (( rc != 0 )); then
    echo "RENDERDOC_CAPTURE_FAIL: renderdoccmd exit=$rc artifacts=$out" >&2
    exit "$rc"
fi
if [[ ! -s "$capture_file" ]]; then
    echo "RENDERDOC_CAPTURE_FAIL: no non-empty .rdc produced" >&2
    exit 1
fi
echo "RENDERDOC_CAPTURE_PASS: $capture_file ($(stat -c '%s' "$capture_file") bytes)"
