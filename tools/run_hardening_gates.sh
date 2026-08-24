#!/usr/bin/env bash
set -euo pipefail

# Canonical local/CI runner for the non-rendering hardening gates.
# Required: BUILD_A, BUILD_B, VCPKG_ROOT.
# Optional: ABI_SO + ABI_BASELINE, RESOURCE_DAEMON + RESOURCE_WORKLOAD.

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_a="${BUILD_A:?BUILD_A must point to first clean artifact tree}"
build_b="${BUILD_B:?BUILD_B must point to second clean artifact tree}"
vcpkg_root="${VCPKG_ROOT:?VCPKG_ROOT must point to vcpkg installed root}"
out="${HARDENING_OUT:-${root}/build/hardening-artifacts}"
mkdir -p "$out"

python3 "$root/tools/check_reproducible_artifacts.py" "$build_a" "$build_b"
python3 "$root/tools/collect_sbom.py" --vcpkg-root "$vcpkg_root" --out "$out/chronon3d-sbom.spdx.json"
budget_args=(
    "$root/tools/check_build_budget.py" "${BUILD_BUDGET_ROOT:-$build_a}"
    --max-bytes "${MAX_BUILD_BYTES:-2147483648}"
    --max-files "${MAX_BUILD_FILES:-250000}"
)
if [[ -n "${BUILD_BINARY:-}" ]]; then budget_args+=(--binary "$BUILD_BINARY"); fi
if [[ -n "${MAX_BUILD_DEPENDENCIES:-}" ]]; then
    budget_args+=(--max-dependencies "$MAX_BUILD_DEPENDENCIES")
fi
budget_args+=(--out "$out/build-metrics.json")
python3 "${budget_args[@]}"

if [[ -n "${RESOURCE_PID:-}" ]]; then
    python3 "$root/tools/resource_audit.py" snapshot "$RESOURCE_PID" --out "$out/resources-before.json"
    if [[ -z "${RESOURCE_AFTER:-}" ]]; then
        echo "RESOURCE_AUDIT_INFO: set RESOURCE_AFTER to compare a post-workload snapshot" >&2
    else
        python3 "$root/tools/resource_audit.py" compare "$out/resources-before.json" "$RESOURCE_AFTER"
    fi
fi

if [[ -n "${RESOURCE_DAEMON:-}" || -n "${RESOURCE_WORKLOAD:-}" ]]; then
    [[ -n "${RESOURCE_DAEMON:-}" && -n "${RESOURCE_WORKLOAD:-}" ]] || {
        echo "RESOURCE_AUDIT_FAIL: RESOURCE_DAEMON and RESOURCE_WORKLOAD must be set together" >&2
        exit 1
    }
    RESOURCE_OUT="$out/resource-leak-certification" \
        bash "$root/tools/check_resource_leak_gate.sh"
fi

if [[ -n "${ABI_SO:-}" ]]; then
    ABI_BASELINE="${ABI_BASELINE:-$root/tools/sdk/chronon3d_c.abi}" \
        bash "$root/tools/sdk/check_abi_libabigail.sh"
fi

echo "HARDENING_GATES_PASS: reproducibility, SBOM, budget${RESOURCE_PID:+, resources}${RESOURCE_DAEMON:+, leak-certification}${ABI_SO:+, ABI}"
