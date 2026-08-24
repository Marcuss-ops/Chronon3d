#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="$(mktemp -d "${TMPDIR:-/tmp}/chronon-crash-artifact.XXXXXX")"
trap 'rm -rf "$out"' EXIT

set +e
bash "$root/tools/capture_crash_artifact.sh" "$out" -- /bin/sh -c 'kill -SEGV $$'
rc=$?
set -e
[[ "$rc" -eq 139 ]] || {
    echo "CRASH_ARTIFACT_TEST_FAIL: expected wrapper exit 139, got $rc" >&2
    exit 1
}

python3 "$root/tools/validate_crash_artifact.py" "$out"
python3 - "$out/result.json" <<'PY'
import json
import sys
result = json.load(open(sys.argv[1]))
assert result["exit_code"] == 139, result
assert result["signal"] == 11, result
PY
echo "CRASH_ARTIFACT_TEST_PASS"
