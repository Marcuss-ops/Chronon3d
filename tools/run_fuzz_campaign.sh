#!/usr/bin/env bash
set -euo pipefail

# Short coverage-guided campaign used by nightly CI.  The corpus directories
# are deliberately caller-owned so CI can upload them as the next campaign's
# persistent corpus.

seconds="${FUZZ_SECONDS:-60}"
artifact_dir="${FUZZ_ARTIFACT_DIR:?FUZZ_ARTIFACT_DIR must point to a writable artifact directory}"
mkdir -p "$artifact_dir"

run_target() {
    local name="$1" binary="$2" corpus="$3"
    [[ -x "$binary" ]] || { echo "FUZZ_FAIL: missing executable $binary" >&2; return 1; }
    mkdir -p "$corpus" "$artifact_dir/$name"
    echo "[fuzz] $name for ${seconds}s"
    timeout --preserve-status "$((seconds + 30))" \
        "$binary" "$corpus" \
        -max_total_time="$seconds" \
        -artifact_prefix="$artifact_dir/$name/" \
        -print_final_stats=1
}

run_target ipc_codec \
    "${IPC_FUZZ_BINARY:?IPC_FUZZ_BINARY is required}" \
    "${IPC_FUZZ_CORPUS:?IPC_FUZZ_CORPUS is required}"
run_target maybe_expression \
    "${EXPRESSION_FUZZ_BINARY:?EXPRESSION_FUZZ_BINARY is required}" \
    "${EXPRESSION_FUZZ_CORPUS:?EXPRESSION_FUZZ_CORPUS is required}"
run_target composition_descriptor \
    "${DESCRIPTOR_FUZZ_BINARY:?DESCRIPTOR_FUZZ_BINARY is required}" \
    "${DESCRIPTOR_FUZZ_CORPUS:?DESCRIPTOR_FUZZ_CORPUS is required}"

echo "FUZZ_CAMPAIGN_PASS: ${seconds}s per target; corpus retained at $artifact_dir"
