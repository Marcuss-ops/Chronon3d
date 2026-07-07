#!/usr/bin/env python3
# ═══════════════════════════════════════════════════════════════════════════
# tools/ae_parity_referee/diff_pixels.py — AE-parity referee pixel-diff helper
# (TICKET-AE-PARITY-DRIVER).
#
# Compares two PNGs (ref vs engine) and emits a single-line PIPE-SEPARATED
# record to stdout:
#
#   OK|mae|psnr|blank|verdict|reasons
#   ERR|error_type|error_detail
#
# where:
#   * mae       — Mean Absolute Error, 0..255 (threshold via AE_PARITY_MAE_THRESHOLD_255).
#   * psnr      — Peak Signal-to-Noise Ratio in dB, or "inf" if MSE==0
#                 (threshold via AE_PARITY_PSNR_THRESHOLD).
#   * blank     — "true" if BOTH images are a single-color flood (anti-greenwashing).
#   * verdict   — "PASS" or "FAIL" (bakes in the threshold check + blank guard).
#   * reasons   — comma-separated reason codes on FAIL (empty on PASS).
#
# Formula parity (intentionally mirrors `tests/visual/support/image_diff.cpp`):
#   * MAE  = (Σ |ΔR|+|ΔG|+|ΔB|) / (W * H * 3)            [units: 0..255]
#   * MSE  = (Σ (ΔR)²+(ΔG)²+(ΔB)²) / (W * H * 3)        [units: 0..65025]
#   * PSNR = 10 * log10(255² / MSE)                       [units: dB]
#
# Anti-greenwashing blank-frame guard (v2 — broader than v1):
#   v1 checked only `ref_pixel_sum == 0 AND eng_pixel_sum == 0` (pure black).
#   v2 checks `ref_min == ref_max AND eng_min == eng_max` (any single-color
#   flood).  Catches pure-red, pure-white, pure-anything — not just black.
#
# Design note: the verdict is computed INSIDE this script (not by the bash
# driver).  This eliminates the shell-injection risk present in the v1 design
# where the bash driver interpolated JSON-derived values into a `python3 -c`
# source string.  All thresholds are read from env vars here, never from bash
# variable interpolation.  The bash driver just reads the verdict from stdout.
#
# Exit codes:
#   0 — diff completed (verdict in output line, either PASS or FAIL)
#   1 — input error (size mismatch, missing file, decode error)
#   2 — usage error
# ═══════════════════════════════════════════════════════════════════════════

import math
import os
import sys

from PIL import Image  # Pillow >= 10 (verified on 11.3.0)


def _err(error_type: str, detail: str) -> None:
    """Emit an ERR pipe-separated record (no stack traces, single line)."""
    print(f"ERR|{error_type}|{detail}")


def _check_inputs(ref_path: str, eng_path: str) -> str | None:
    """Return an error_type string if inputs are invalid, else None.

    No pre-decode size guard: a valid 1x1 PNG is ~67 bytes and a 10x10
    uniform PNG is ~77 bytes, so any byte-based threshold is fragile
    (the v1 guard of 256 bytes incorrectly rejected small test PNGs).
    PIL's Image.open() handles all PNG validation; we rely on the
    _decode_pngs() exception path to catch truncated/corrupt files.
    """
    if not os.path.isfile(ref_path):
        return f"ref_missing:{ref_path}"
    if not os.path.isfile(eng_path):
        return f"eng_missing:{eng_path}"
    return None


def _decode_pngs(ref_path: str, eng_path: str) -> tuple | None:
    """Decode + convert both PNGs to RGB.  Returns (ref, eng) or (None, err)."""
    try:
        ref = Image.open(ref_path).convert("RGB")
    except Exception as ex:  # pragma: no cover — defensive only
        return (None, f"ref_decode:{ex}")
    try:
        eng = Image.open(eng_path).convert("RGB")
    except Exception as ex:  # pragma: no cover — defensive only
        return (None, f"eng_decode:{ex}")
    if ref.size != eng.size:
        return (None, f"size_mismatch:{list(ref.size)}vs{list(eng.size)}")
    return (ref, eng)


def _compute_metrics(ref_bytes: bytes, eng_bytes: bytes) -> dict:
    """Single-pass: MAE + MSE + min/max for the broader blank-frame guard."""
    total_abs = 0
    total_sq = 0
    # Track min/max per image (anti-greenwashing v2: any uniform color = blank).
    ref_min, ref_max = 255, 0
    eng_min, eng_max = 255, 0
    for r, e in zip(ref_bytes, eng_bytes):
        d = r - e
        total_abs += abs(d)
        total_sq += d * d
        if r < ref_min: ref_min = r
        if r > ref_max: ref_max = r
        if e < eng_min: eng_min = e
        if e > eng_max: eng_max = e
    return {
        "total_abs": total_abs,
        "total_sq": total_sq,
        "ref_min": ref_min, "ref_max": ref_max,
        "eng_min": eng_min, "eng_max": eng_max,
    }


def compute_diff(ref_path: str, eng_path: str) -> str:
    """Compute MAE + PSNR + blank-guard + verdict.  Return pipe-separated line."""
    err_type = _check_inputs(ref_path, eng_path)
    if err_type is not None:
        _err(err_type.split(":", 1)[0], err_type.split(":", 1)[1] if ":" in err_type else err_type)
        return ""

    decoded = _decode_pngs(ref_path, eng_path)
    if decoded[0] is None:
        _err(decoded[1].split(":", 1)[0], decoded[1].split(":", 1)[1] if ":" in decoded[1] else decoded[1])
        return ""

    ref, eng = decoded
    width, height = ref.size
    metrics = _compute_metrics(ref.tobytes(), eng.tobytes())

    n = width * height * 3
    mae_255 = metrics["total_abs"] / n
    mse = metrics["total_sq"] / n
    if mse == 0:
        psnr_db = float("inf")
        psnr_str = "inf"
    else:
        psnr_db = 10.0 * math.log10((255.0 * 255.0) / mse)
        psnr_str = f"{psnr_db:.4f}"

    # Anti-greenwashing v2: ANY single-color flood (not just pure black).
    blank = (metrics["ref_min"] == metrics["ref_max"]) and \
            (metrics["eng_min"] == metrics["eng_max"])

    # Thresholds via env vars (NO bash interpolation, no shell injection).
    mae_threshold = float(os.environ.get("AE_PARITY_MAE_THRESHOLD_255", "5"))
    psnr_threshold = float(os.environ.get("AE_PARITY_PSNR_THRESHOLD", "30"))

    ok_mae = mae_255 < mae_threshold
    ok_psnr = (psnr_db == float("inf")) or (psnr_db > psnr_threshold)
    ok_blank = not blank

    if ok_mae and ok_psnr and ok_blank:
        verdict = "PASS"
        reasons = ""
    else:
        verdict = "FAIL"
        reasons_list = []
        if not ok_mae: reasons_list.append(f"mae>={mae_threshold}")
        if not ok_psnr: reasons_list.append(f"psnr<={psnr_threshold}")
        if not ok_blank: reasons_list.append("blank")
        reasons = ",".join(reasons_list)

    return f"OK|{mae_255:.4f}|{psnr_str}|{str(blank).lower()}|{verdict}|{reasons}"


def main() -> int:
    if len(sys.argv) != 3:
        _err("usage", "expected: ref.png engine.png")
        return 2

    line = compute_diff(sys.argv[1], sys.argv[2])
    if not line:
        return 1
    print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
