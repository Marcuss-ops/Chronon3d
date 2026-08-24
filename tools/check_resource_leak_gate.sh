#!/usr/bin/env bash
set -euo pipefail

# End-to-end Linux leak certification for a long-lived daemon.
# Required: RESOURCE_DAEMON and RESOURCE_WORKLOAD shell command strings.
# Optional: RESOURCE_READY, RESOURCE_ALLOW='fd=0 mmap_regions=0 threads=0'.

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
daemon_cmd="${RESOURCE_DAEMON:?RESOURCE_DAEMON must start the daemon}"
workload_cmd="${RESOURCE_WORKLOAD:?RESOURCE_WORKLOAD must exercise the daemon}"
out="${RESOURCE_OUT:-${root}/build/resource-leak-certification}"
mkdir -p "$out"

cleanup() {
    if [[ -n "${daemon_pid:-}" ]] && kill -0 "$daemon_pid" 2>/dev/null; then
        kill -TERM "$daemon_pid" 2>/dev/null || true
        wait "$daemon_pid" 2>/dev/null || true
    fi
}
trap cleanup EXIT

bash -c "$daemon_cmd" >"$out/daemon.stdout.log" 2>"$out/daemon.stderr.log" &
daemon_pid=$!
if [[ -n "${RESOURCE_READY:-}" ]]; then
    bash -c "$RESOURCE_READY"
else
    sleep "${RESOURCE_START_DELAY:-1}"
fi

python3 "$root/tools/resource_audit.py" snapshot "$daemon_pid" --out "$out/before.json"
bash -c "$workload_cmd"
python3 "$root/tools/resource_audit.py" snapshot "$daemon_pid" --out "$out/after.json"

args=()
for allowance in ${RESOURCE_ALLOW:-}; do
    args+=(--allow "$allowance")
done
python3 "$root/tools/resource_audit.py" compare "$out/before.json" "$out/after.json" "${args[@]}"
echo "RESOURCE_LEAK_CERTIFIED: daemon_pid=$daemon_pid artifacts=$out"
