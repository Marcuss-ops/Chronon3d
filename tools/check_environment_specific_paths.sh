#!/usr/bin/env bash
set -euo pipefail

# Build configuration must discover CUDA/FFmpeg from the host environment.
# A path from a developer workstation or a temporary build directory is never
# a valid dependency declaration in the source tree.
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
scan_paths=("$root/apps" "$root/src" "$root/include" "$root/tools" "$root/docs/CURRENT_STATUS.md" "$root/CMakeLists.txt")
for candidate in "$@"; do
    if [[ -e "$candidate" ]]; then
        scan_paths+=("$candidate")
    else
        echo "GATE_FAIL: requested scan path does not exist: $candidate" >&2
        exit 1
    fi
done

if rg -n --hidden --glob '!vcpkg_installed/**' \
    --glob '!tools/check_environment_specific_paths.sh' \
    '/usr/local/lib/python|/tmp/velox-cuda-dev|libnvrtc\.so\.13|libnvrtc-builtins\.so\.13\.0' \
    "${scan_paths[@]}"; then
    echo "GATE_FAIL: host-specific CUDA paths or sonames are present" >&2
    exit 1
fi
echo "GATE_PASS: CUDA discovery is environment/toolchain based"
