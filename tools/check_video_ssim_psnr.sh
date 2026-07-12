#!/usr/bin/env bash
# check_video_ssim_psnr.sh — canonical FAIL-LOUD gate for spec §7 Video
# Completeness Matrix (SSIM + PSNR raw-vs-decoded comparison).
#
# Per AGENTS.md §honest-limitation + §INFO-level diagnostic style + Cat-3 zero
# new public SDK API surface:
#
#   Step 0:    precondition via canonical tools/check_ffmpeg_required.sh
#              (FAIL-LOUD with em-dash canonical `apt install ffmpeg` hint).
#   Step 0.5:  dependency-on-prev-gate assertion — the prior gate
#              `tools/check_video_completeness.sh` (Step 4.5h in wrap_push chain)
#              must have produced the canonical 60-frame decoded PNGs at
#              $OUTPUT_DIR/decoded_frames AND the raw PNG sequence at
#              $OUTPUT_DIR/raw_frames (env-override-able for cross-host CI).
#              Fail-loud with canonical hint if either is missing/partial.
#   Step 1:    SSIM comparison via `ffmpeg -lavfi ssim=stats_file=ssim.log -f null`
#              reading raw PNGs + decoded PNGs → writes ssim.log (per-frame + summary).
#   Step 2:    PSNR comparison via `ffmpeg -lavfi psnr=stats_file=psnr.log -f null`
#              reading raw PNGs + decoded PNGs → writes psnr.log (per-frame + summary).
#   Step 3:    Python-side parse + 3-threshold assertion (user-spec verbatim):
#              avg_ssim ≥ 0.97 + avg_psnr ≥ 35 dB + min_frame_ssim ≥ 0.94.
#              Forward-point: post encoder-tuning tighten to ≥0.985 + ≥40 dB
#              is `TICKET-VIDEO-SSIM-PSNR-ENCODER-TUNING-TIGHTENING` (separate
#              ticket per Cat-3 anti-duplication).
#              Writes `ssim_psnr_summary.txt` (human-readable summary).
#   Step 4:    PASS canonical + addizionale [INFO] ${GATE_NAME}: … line per
#              AGENTS.md `## Regole di lint documentale` INFO-level diagnostic
#              style rule (≤200 chars, grep-discoverable).
#
# Exit codes: 0 = GATE_PASS, 1 = GATE_FAIL (dependency / assertion),
#             2 = GATE_FAIL_INTERNAL (ffmpeg-missing / parse-fail).
#
# Cross-link: `tools/check_video_completeness.sh` (the canonical ffprobe+decode
# gate at Step 4.5h in wrap_push chain, the upstream dependency for this
# gate's decoded_frames PNG sequence); `tools/check_ffmpeg_required.sh`
# (Step 0 precondition); `tests/text/test_pipeline_parity_real.cpp::Fase 6 §5`
# (commit `e689820b`, produces the raw PNGs at
# $CHRONON3D_TEST_VIDEO_OUTPUT_DIR/raw_frames/frame_NNNNNN.png).
set -euo pipefail

GATE_NAME=check_video_ssim_psnr
readonly GATE_NAME

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"
readonly SCRIPT_DIR
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." &>/dev/null && pwd)"
readonly REPO_ROOT

# ── Step 0: precondition via canonical ffmpeg/ffprobe FAIL-LOUD gate ──────
bash "${SCRIPT_DIR}/check_ffmpeg_required.sh"

# ── Resolve paths (env-override-able for cross-host CI) ──────────────────
OUTPUT_DIR="${CHRONON3D_TEST_VIDEO_OUTPUT_DIR:-${REPO_ROOT}/output/text_video_acceptance}"
OUTPUT_DIR="$(cd -- "${OUTPUT_DIR}" &>/dev/null && pwd)"
RAW_DIR="${OUTPUT_DIR}/raw_frames"
DECODED_DIR="${OUTPUT_DIR}/decoded_frames"
SSIM_LOG="${OUTPUT_DIR}/ssim.log"
PSNR_LOG="${OUTPUT_DIR}/psnr.log"
SUMMARY="${OUTPUT_DIR}/ssim_psnr_summary.txt"
readonly OUTPUT_DIR RAW_DIR DECODED_DIR SSIM_LOG PSNR_LOG SUMMARY

# ── Step 0.5: dependency-on-prev-gate assertion (30-frames side both dirs) ─
if [[ ! -d "${DECODED_DIR}" ]]; then
    echo "GATE_FAIL: ${GATE_NAME}: dependency missing — prior tools/check_video_completeness.sh must run first" >&2
    echo "  expected decoded PNGs at ${DECODED_DIR}" >&2
    echo "  hint: bash ${SCRIPT_DIR}/check_video_completeness.sh" >&2
    exit 1
fi
DECODED_COUNT="$(find "${DECODED_DIR}" -type f -name 'frame_*.png' | wc -l | tr -d ' ')"
if [[ "${DECODED_COUNT}" != "60" ]]; then
    echo "GATE_FAIL: ${GATE_NAME}: dependency partial — ${DECODED_DIR} has ${DECODED_COUNT} frame_*.png, expected 60" >&2
    echo "  hint: re-run bash ${SCRIPT_DIR}/check_video_completeness.sh" >&2
    exit 1
fi
if [[ ! -d "${RAW_DIR}" ]]; then
    echo "GATE_FAIL: ${GATE_NAME}: raw PNG sequence missing at ${RAW_DIR}" >&2
    echo "  hint: run tests/text/test_pipeline_parity_real.cpp::Fase 6 §5 first (commit e689820b)" >&2
    exit 1
fi
RAW_COUNT="$(find "${RAW_DIR}" -type f -name 'frame_*.png' | wc -l | tr -d ' ')"
if [[ "${RAW_COUNT}" != "60" ]]; then
    echo "GATE_FAIL: ${GATE_NAME}: raw PNG sequence partial — ${RAW_DIR} has ${RAW_COUNT} frame_*.png, expected 60" >&2
    exit 1
fi

# ── Step 1: SSIM comparison raw vs decoded → ssim.log ─────────────────────
ffmpeg -y -hide_banner -loglevel warning \
    -i "${RAW_DIR}/frame_%06d.png" \
    -i "${DECODED_DIR}/frame_%06d.png" \
    -lavfi ssim=stats_file="${SSIM_LOG}" \
    -f null - 2>&1 | tail -5 >/dev/null || {
    echo "GATE_FAIL: ${GATE_NAME}: ffmpeg ssim comparison failed" >&2
    exit 1
}

# ── Step 2: PSNR comparison raw vs decoded → psnr.log ────────────────────
ffmpeg -y -hide_banner -loglevel warning \
    -i "${RAW_DIR}/frame_%06d.png" \
    -i "${DECODED_DIR}/frame_%06d.png" \
    -lavfi psnr=stats_file="${PSNR_LOG}" \
    -f null - 2>&1 | tail -5 >/dev/null || {
    echo "GATE_FAIL: ${GATE_NAME}: ffmpeg psnr comparison failed" >&2
    exit 1
}

# ── Step 3: Python-side parse + 3-threshold assertion + summary emit ──────
# Pass 5 args: SSIM_LOG + PSNR_LOG + SUMMARY + RAW_DIR + DECODED_DIR (the latter
# two for human-readable summary display; canonical fix-forward per prior code-reviewer
# NEEDS-FIX CV-3 + CV-8 [Python FILE refs]). Robust against FileNotFoundError per
# AGENTS.md §honest-limitation FAIL-LOUD contract.
python3 - "${SSIM_LOG}" "${PSNR_LOG}" "${SUMMARY}" "${RAW_DIR}" "${DECODED_DIR}" <<'PY' || exit 1
import re, sys
ssim_log, psnr_log, summary_path, raw_dir, decoded_dir = sys.argv[1:6]

SSIM_MIN_AVG   = 0.97      # user-spec verbatim (§7 initial gate threshold)
PSNR_MIN_AVG   = 35.0      # user-spec verbatim (§7 initial gate threshold)
SSIM_MIN_FLOOR = 0.94      # user-spec verbatim (§7 no-frame-below invariant)

def fail(msg):
    print(f'GATE_FAIL: check_video_ssim_psnr: {msg}', file=sys.stderr)
    sys.exit(1)

# Read SSIM log + PSNR log with FAIL-LOUD handling on missing files (CV-3 fix-forward
# per prior code-reviewer; bare `open()` raises NameError-equivalent traceback on
# missing file, which violates AGENTS.md §honest-limitation's canonical-hint contract).
try:
    with open(ssim_log) as f:
        ssim_lines = f.read().splitlines()
except FileNotFoundError:
    fail(f'SSIM log missing at {ssim_log}; Step 1 ffmpeg -lavfi ssim=stats_file=... should have written the log; investigate ffmpeg exit code (Step 1 [$-SSIM_LOG] hint: re-run with `bash -x tools/check_video_ssim_psnr.sh` for trace)')
try:
    with open(psnr_log) as f:
        psnr_lines = f.read().splitlines()
except FileNotFoundError:
    fail(f'PSNR log missing at {psnr_log}; Step 2 ffmpeg -lavfi psnr=stats_file=... should have written the log; investigate ffmpeg exit code')

# SSIM log ffmpeg-format per-frame "n:1 Y:.. U:.. V:.. All:.. (secs)" lines +
# final "SSIM ... Avg: ..." summary line. The Avg: summary line has its own
# All: token; we skip it (the average is the mean of per-frame values, which
# is what the ffmpeg filter emits anyway).
ssim_values = []
for line in ssim_lines:
    if "Avg:" in line:
        continue
    for tok in re.findall(r'\bAll:([\d.]+)', line):
        ssim_values.append(float(tok))
if not ssim_values:
    fail(f'ssim.log has no parseable All: values at {ssim_log}')

avg_ssim = sum(ssim_values) / len(ssim_values)
min_ssim = min(ssim_values)
max_ssim = max(ssim_values)

# PSNR log ffmpeg-format per-frame "n:1 mse_avg:.. psnr_avg:.." + final
# "PSNR ... psnr_avg:inf" (when frames bit-identical). Handle inf cleanly.
psnr_values = []
finite_psnr = []
for line in psnr_lines:
    for tok in re.findall(r'\bpsnr_avg:([\d.]+|inf)\b', line):
        psnr_values.append(float('inf') if tok == 'inf' else float(tok))
        if tok != 'inf':
            finite_psnr.append(float(tok))
if not psnr_values:
    fail(f'psnr.log has no parseable psnr_avg: values at {psnr_log}')

avg_psnr = (sum(finite_psnr) / len(finite_psnr)) if finite_psnr else float('inf')

# Threshold assertions (user-spec verbatim).
if avg_ssim < SSIM_MIN_AVG:
    fail(f"avg SSIM < {SSIM_MIN_AVG} (got {avg_ssim:.4f}); tighten post encoder-tuning is TICKET-VIDEO-SSIM-PSNR-ENCODER-TUNING-TIGHTENING (separate ticket per Cat-3)")
if avg_psnr != float('inf') and avg_psnr < PSNR_MIN_AVG:
    fail(f"avg PSNR < {PSNR_MIN_AVG} dB (got {avg_psnr:.4f}); tighten post encoder-tuning")
if min_ssim < SSIM_MIN_FLOOR:
    fail(f"min frame SSIM < {SSIM_MIN_FLOOR} (got {min_ssim:.4f} at one of {len(ssim_values)} frames)")

# Human-readable summary emit (with canonical RAW_DIR + DECODED_DIR display).
with open(summary_path, 'w') as f:
    f.write("SSIM/PSNR summary — tools/check_video_ssim_psnr.sh\n")
    f.write("==============================================\n")
    f.write(f"input raw PNG sequence:     {raw_dir} (60 frames)\n")
    f.write(f"input decoded PNG sequence: {decoded_dir} (60 frames)\n")
    f.write(f"ssim.log path:              {ssim_log}\n")
    f.write(f"psnr.log path:              {psnr_log}\n")
    f.write(f"----------------------------------------------\n")
    f.write(f"avg_ssim            = {avg_ssim:.4f}   (threshold >= {SSIM_MIN_AVG})\n")
    if avg_psnr == float('inf'):
        f.write(f"avg_psnr            = inf dB       (frames bit-identical, threshold >= {PSNR_MIN_AVG} dB)\n")
    else:
        f.write(f"avg_psnr            = {avg_psnr:.4f}   (threshold >= {PSNR_MIN_AVG} dB)\n")
    f.write(f"min_ssim            = {min_ssim:.4f}   (threshold >= {SSIM_MIN_FLOOR})\n")
    f.write(f"max_ssim            = {max_ssim:.4f}\n")
    f.write(f"frames_compared     = {len(ssim_values)}\n")

# Parallel emit (for bash to capture metrics into the [INFO] line on PASS; canonical
# fix-forward per prior code-reviewer CV-7 [INFO] metrics density).
import json as _json_for_bash_capture
print(_json_for_bash_capture.dumps({
    'avg_ssim': round(avg_ssim, 4),
    'avg_psnr_finite': avg_psnr != float('inf'),
    'avg_psnr': 999.99 if avg_psnr == float('inf') else round(avg_psnr, 4),
    'min_ssim': round(min_ssim, 4),
    'max_ssim': round(max_ssim, 4),
    'frames_compared': len(ssim_values),
}))
PY

# ── Step 4: PASS canonical + addizionale [INFO] line per AGENTS.md style ─
# Capture metrics from the previous Python heredoc (stdout emits a single JSON line)
# for [INFO] line metrics density (CV-7 fix-forward per prior code-reviewer).
PYTHON_OUTPUT=$(python3 - "${SSIM_LOG}" "${PSNR_LOG}" "${SUMMARY}" "${RAW_DIR}" "${DECODED_DIR}" <<'PY' || exit 1
import re, sys, json
ssim_log, psnr_log, summary_path, raw_dir, decoded_dir = sys.argv[1:6]

SSIM_MIN_AVG   = 0.97
PSNR_MIN_AVG   = 35.0
SSIM_MIN_FLOOR = 0.94

def fail(msg):
    print(f'GATE_FAIL: check_video_ssim_psnr: {msg}', file=sys.stderr)
    sys.exit(1)

try:
    with open(ssim_log) as f:
        ssim_lines = f.read().splitlines()
except FileNotFoundError:
    fail(f'SSIM log missing at {ssim_log}')
try:
    with open(psnr_log) as f:
        psnr_lines = f.read().splitlines()
except FileNotFoundError:
    fail(f'PSNR log missing at {psnr_log}')

ssim_values = []
for line in ssim_lines:
    if "Avg:" in line:
        continue
    for tok in re.findall(r'\bAll:([\d.]+)', line):
        ssim_values.append(float(tok))
if not ssim_values:
    fail(f'ssim.log has no parseable All: values at {ssim_log}')

avg_ssim = sum(ssim_values) / len(ssim_values)
min_ssim = min(ssim_values)
max_ssim = max(ssim_values)

psnr_values = []
finite_psnr = []
for line in psnr_lines:
    for tok in re.findall(r'\bpsnr_avg:([\d.]+|inf)\b', line):
        psnr_values.append(float('inf') if tok == 'inf' else float(tok))
        if tok != 'inf':
            finite_psnr.append(float(tok))

avg_psnr = (sum(finite_psnr) / len(finite_psnr)) if finite_psnr else float('inf')

if avg_ssim < SSIM_MIN_AVG:
    fail(f"avg SSIM < {SSIM_MIN_AVG} (got {avg_ssim:.4f})")
if avg_psnr != float('inf') and avg_psnr < PSNR_MIN_AVG:
    fail(f"avg PSNR < {PSNR_MIN_AVG} dB (got {avg_psnr:.4f})")
if min_ssim < SSIM_MIN_FLOOR:
    fail(f"min frame SSIM < {SSIM_MIN_FLOOR} (got {min_ssim:.4f})")

# Human-readable summary emit.
with open(summary_path, 'w') as f:
    f.write("SSIM/PSNR summary — tools/check_video_ssim_psnr.sh\n")
    f.write("==============================================\n")
    f.write(f"input raw PNG sequence:     {raw_dir} (60 frames)\n")
    f.write(f"input decoded PNG sequence: {decoded_dir} (60 frames)\n")
    f.write(f"ssim.log path:              {ssim_log}\n")
    f.write(f"psnr.log path:              {psnr_log}\n")
    f.write(f"----------------------------------------------\n")
    f.write(f"avg_ssim            = {avg_ssim:.4f}   (threshold >= {SSIM_MIN_AVG})\n")
    if avg_psnr == float('inf'):
        f.write(f"avg_psnr            = inf dB       (frames bit-identical, threshold >= {PSNR_MIN_AVG} dB)\n")
    else:
        f.write(f"avg_psnr            = {avg_psnr:.4f}   (threshold >= {PSNR_MIN_AVG} dB)\n")
    f.write(f"min_ssim            = {min_ssim:.4f}   (threshold >= {SSIM_MIN_FLOOR})\n")
    f.write(f"max_ssim            = {max_ssim:.4f}\n")
    f.write(f"frames_compared     = {len(ssim_values)}\n")

# Emit metrics JSON on stdout for bash to consume into [INFO] line.
print(json.dumps({
    'avg_ssim': round(avg_ssim, 4),
    'avg_psnr_finite': avg_psnr != float('inf'),
    'avg_psnr': 999.99 if avg_psnr == float('inf') else round(avg_psnr, 4),
    'min_ssim': round(min_ssim, 4),
    'max_ssim': round(max_ssim, 4),
    'frames_compared': len(ssim_values),
}))
PY
)

# Parse metrics from Python output (JSON single line on stdout).
AVG_SSIM=$(echo "${PYTHON_OUTPUT}" | python3 -c 'import json,sys; print(f"{json.loads(sys.stdin.read())[\"avg_ssim\"]:.4f}")')
AVG_PSNR_FINITE=$(echo "${PYTHON_OUTPUT}" | python3 -c 'import json,sys; d=json.loads(sys.stdin.read()); print("inf" if d["avg_psnr_finite"] else f"{d[\"avg_psnr\"]:.2f}")')
MIN_SSIM=$(echo "${PYTHON_OUTPUT}" | python3 -c 'import json,sys; print(f"{json.loads(sys.stdin.read())[\"min_ssim\"]:.4f}")')

echo "GATE_PASS: ${GATE_NAME}: SSIM/PSNR 60-frame comparison verified — avg_ssim=${AVG_SSIM} + avg_psnr=${AVG_PSNR_FINITE} + min_frame_ssim=${MIN_SSIM}"
INFO_LINE="[INFO] ${GATE_NAME}: SSIM/PSNR 60-frame PASS — avg_ssim=${AVG_SSIM} avg_psnr=${AVG_PSNR_FINITE}dB min_ssim=${MIN_SSIM}  (see ${SUMMARY})"
if [[ ${#INFO_LINE} -gt 200 ]]; then
    INFO_LINE="${INFO_LINE:0:197}..."
fi
echo "${INFO_LINE}"

exit 0
