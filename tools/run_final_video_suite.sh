#!/usr/bin/env bash
# Run the canonical real-MP4 acceptance suite and emit one telemetry sidecar
# next to every artifact.  This is intentionally a runner, not a second
# renderer: chronon3d_cli remains the sole JSON/composition → MP4 pipeline.
set -euo pipefail

GATE_NAME=run_final_video_suite
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
MANIFEST="${CHRONON3D_FINAL_SUITE_MANIFEST:-${REPO_ROOT}/examples/final_video_suite.json}"
OUTPUT_DIR="${CHRONON3D_FINAL_SUITE_OUTPUT:-${REPO_ROOT}/output/final_video_suite}"
KEEP_FRAMES="${CHRONON3D_FINAL_SUITE_KEEP_FRAMES:-0}"

if [[ "${OUTPUT_DIR}" == "${REPO_ROOT}" || "${OUTPUT_DIR}" == "/" || -z "${OUTPUT_DIR}" ]]; then
    echo "GATE_FAIL: ${GATE_NAME}: refusing unsafe output directory '${OUTPUT_DIR}'" >&2
    exit 1
fi
command -v python3 >/dev/null 2>&1 || { echo "GATE_FAIL: ${GATE_NAME}: python3 not found" >&2; exit 1; }
command -v ffprobe >/dev/null 2>&1 || { echo "GATE_FAIL: ${GATE_NAME}: ffprobe not found" >&2; exit 1; }
command -v ffmpeg >/dev/null 2>&1 || { echo "GATE_FAIL: ${GATE_NAME}: ffmpeg not found" >&2; exit 1; }
command -v /usr/bin/time >/dev/null 2>&1 || { echo "GATE_FAIL: ${GATE_NAME}: /usr/bin/time not found" >&2; exit 1; }

if [[ "${KEEP_FRAMES}" != 0 && "${KEEP_FRAMES}" != 1 ]]; then
    echo "GATE_FAIL: ${GATE_NAME}: CHRONON3D_FINAL_SUITE_KEEP_FRAMES must be 0 or 1" >&2
    exit 1
fi

CHRONON_CLI="${CHRONON3D_CLI:-}"
if [[ -z "${CHRONON_CLI}" ]]; then
    for candidate in \
        "$(command -v chronon3d_cli 2>/dev/null || true)" \
        "${REPO_ROOT}/build/chronon/linux-ci-full-validation/apps/chronon3d_cli/chronon3d_cli" \
        "${REPO_ROOT}/build/chronon/linux-fast-dev/apps/chronon3d_cli/chronon3d_cli" \
        "${REPO_ROOT}/build/apps/chronon3d_cli/chronon3d_cli"; do
        if [[ -x "${candidate}" ]]; then CHRONON_CLI="${candidate}"; break; fi
    done
fi
[[ -x "${CHRONON_CLI}" ]] || { echo "GATE_FAIL: ${GATE_NAME}: chronon3d_cli not found" >&2; exit 1; }
[[ -f "${MANIFEST}" ]] || { echo "GATE_FAIL: ${GATE_NAME}: manifest missing: ${MANIFEST}" >&2; exit 1; }

# Do not allow a real-MP4 baseline to run against an older renderer binary.
# This is especially important for SIMD/DOF changes: a stale executable can
# report a failure that no longer exists in the checked-out source tree.
for source in \
    "${REPO_ROOT}/include/chronon3d/backends/software/effects/per_pixel_dof.hpp" \
    "${REPO_ROOT}/src/backends/software/simd/highway_dof_kernels.cpp" \
    "${REPO_ROOT}/src/render_graph/nodes/per_pixel_dof_node.cpp"; do
    if [[ -f "${source}" && "${source}" -nt "${CHRONON_CLI}" ]]; then
        echo "GATE_FAIL: ${GATE_NAME}: CLI is older than ${source}; rebuild chronon3d_cli before rendering" >&2
        exit 1
    fi
done

mkdir -p "${OUTPUT_DIR}"
TELEMETRY_DIR="${OUTPUT_DIR}/telemetry"
mkdir -p "${TELEMETRY_DIR}"

python3 - "${MANIFEST}" "${OUTPUT_DIR}" <<'PY'
import json, pathlib, sys
manifest = json.load(open(sys.argv[1]))
out = pathlib.Path(sys.argv[2])
for case in manifest["cases"]:
    (out / f'{case["id"]}-{case["name"]}.case.json').write_text(
        json.dumps(case, indent=2, sort_keys=True) + "\n")
PY

run_case() {
    local id="$1" name="$2" composition="$3" frames="$4"
    local last_frame=$((frames - 1))
    local mp4="${OUTPUT_DIR}/${id}-${name}.mp4"
    local log="${OUTPUT_DIR}/${id}-${name}.log"
    local time_log="${OUTPUT_DIR}/${id}-${name}.time.log"
    local frames_dir="${OUTPUT_DIR}/${id}-${name}.frames"

    echo "[${id}] ${composition} → ${mp4} (${frames} frames)"
    local -a render_args=(
        render "${composition}"
        --frames "0-${last_frame}" --fps 30
        --output "${mp4}" --ffmpeg-mode pipe --report
    )
    if [[ "${KEEP_FRAMES}" == 1 ]]; then
        render_args+=(--keep-frames --frames-dir "${frames_dir}")
    fi
    env CHRONON3D_TELEMETRY_PATH="${TELEMETRY_DIR}" \
        /usr/bin/time -v -o "${time_log}" \
        "${CHRONON_CLI}" "${render_args[@]}" >"${log}" 2>&1

    python3 "${SCRIPT_DIR}/final_video_telemetry.py" \
        --manifest "${MANIFEST}" --case-id "${id}" \
        --mp4 "${mp4}" --telemetry-dir "${TELEMETRY_DIR}" \
        --time-log "${time_log}" --log "${log}" \
        --output "${mp4%.mp4}.telemetry.json"
}

mapfile -t CASES < <(python3 - "${MANIFEST}" <<'PY'
import json, sys
for c in json.load(open(sys.argv[1]))["cases"]:
    print("\t".join((c["id"], c["name"], c["composition"], str(c["frames"]))))
PY
)

for row in "${CASES[@]}"; do
    IFS=$'\t' read -r id name composition frames <<<"${row}"
    run_case "${id}" "${name}" "${composition}" "${frames}"
done

echo "GATE_PASS: ${GATE_NAME}: ${#CASES[@]}/${#CASES[@]} MP4 artifacts and telemetry sidecars generated"
echo "[INFO] ${GATE_NAME}: artifacts=${OUTPUT_DIR} manifest=${MANIFEST}"
