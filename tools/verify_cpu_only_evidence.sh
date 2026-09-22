#!/usr/bin/env bash
set -euo pipefail

# Same-SHA CPU evidence for the contracts that do not require a GPU.  The
# script deliberately reports GPU/CUDA as NOT RUN; it is not a substitute for
# hardware certification.

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${1:-${repo_root}/build/chronon/linux-video-test}"
test_bin="${build_dir}/tests/chronon3d_cli_tests"
jobs="${JOBS:-4}"

if [[ ! -x "${test_bin}" ]]; then
    echo "missing native CLI test binary: ${test_bin}" >&2
    exit 2
fi
if ! command -v ffmpeg >/dev/null 2>&1; then
    echo "ffmpeg executable is required for CPU native evidence" >&2
    exit 2
fi

native_flag="$(cmake -LA -N "${build_dir}" 2>/dev/null |
    awk -F= '$1 == "CHRONON3D_ENABLE_NATIVE_FFMPEG:BOOL" { print $2 }')"
if [[ "${native_flag}" != "ON" ]]; then
    echo "build is not native-FFmpeg enabled: ${build_dir}" >&2
    exit 2
fi

source_sha="$(git -C "${repo_root}" rev-parse HEAD)"
worktree_state="$(git -C "${repo_root}" status --porcelain)"
echo "CPU evidence source_sha=${source_sha}"
echo "build_dir=${build_dir}"
echo "ffmpeg=$(ffmpeg -version | sed -n '1p')"
if [[ -n "${worktree_state}" ]]; then
    echo "worktree_state=DIRTY (tests certify this tree, not the commit SHA)"
else
    echo "worktree_state=CLEAN"
fi

cmake --build "${build_dir}" --target chronon3d_cli_tests -j"${jobs}"

run_case() {
    local name="$1"
    echo "RUN ${name}"
    "${test_bin}" --test-case="${name}"
}

run_case "A1 CPU native export publishes MP4 sidecar receipt and fails closed"
run_case "A1 direct native export route renders a software composition"
run_case "B3 CPU native segment assembly normalizes timestamps and checksums output"

echo "RUN registered CLI contract suite"
ctest --test-dir "${build_dir}" -R '^chronon3d_cli_tests$' --output-on-failure

echo "RUN architecture gate"
python3 "${repo_root}/tools/check_architecture.py" \
    --root "${repo_root}" \
    --rules "${repo_root}/tools/architecture_rules_inc.toml" \
    --rule-prefix handwritten_inc_debt_ratchet

echo "RUN text header fan-out gate"
python3 "${repo_root}/tools/check_text_header_fanout.py" --root "${repo_root}"

echo "RUN diff hygiene"
git -C "${repo_root}" diff --check

inc_count="$(find "${repo_root}/apps" "${repo_root}/src" "${repo_root}/include" \
    -type f -name '*.inc' -print | wc -l | tr -d ' ')"
echo "handwritten_inc_count=${inc_count}"
echo "GPU_CUDA_CERTIFICATION=NOT_RUN"
echo "GITHUB_CI_SAME_SHA=NOT_RUN"
if [[ -n "${worktree_state}" ]]; then
    echo "CPU_EVIDENCE=PASS_SAME_TREE"
    echo "SAME_SHA_ELIGIBLE=NO_DIRTY_WORKTREE"
else
    echo "CPU_EVIDENCE=PASS_SAME_SHA"
    echo "SAME_SHA_ELIGIBLE=YES"
fi
