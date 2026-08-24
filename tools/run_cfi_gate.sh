#!/usr/bin/env bash
set -euo pipefail

# Configure and build the daemon with the checked-in Clang CFI preset.
# This is intentionally a hard gate: silently falling back to GCC would make
# a green CFI job meaningless.
command -v clang >/dev/null 2>&1 || {
    echo "CFI_GATE_FAIL: clang is required" >&2
    exit 2
}
command -v clang++ >/dev/null 2>&1 || {
    echo "CFI_GATE_FAIL: clang++ is required" >&2
    exit 2
}

cmake --preset linux-cfi
cmake --build --preset linux-cfi -j12

compile_db="build/chronon/linux-cfi/compile_commands.json"
[[ -f "$compile_db" ]] || {
    echo "CFI_GATE_FAIL: missing $compile_db" >&2
    exit 1
}
grep -q -- '-fsanitize=cfi' "$compile_db" || {
    echo "CFI_GATE_FAIL: compile database does not contain CFI instrumentation" >&2
    exit 1
}
grep -q -- '-flto' "$compile_db" || {
    echo "CFI_GATE_FAIL: compile database does not contain LTO" >&2
    exit 1
}

daemon="build/chronon/linux-cfi/apps/chronon3d_daemon/chronon3d_daemon"
[[ -x "$daemon" ]] || {
    echo "CFI_GATE_FAIL: daemon artifact not found at $daemon" >&2
    exit 1
}
echo "CFI_GATE_PASS: $daemon"
