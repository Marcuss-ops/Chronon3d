#!/usr/bin/env python3
# ---------------------------------------------------------------------------
# tools/visual_regression_gate.py — Two-tier visual regression gate
#
# Tier 1 — EXACT_SHA
#   SHA-256 of raw RGBA pixel bytes (no headers, no metadata).
#   PASS if the rendered PNG is byte-for-pixel identical to the reference.
#   This answers: "Is Chronon3D deterministic on a certified environment?"
#
# Tier 2 — VISUAL_SCORE
#   Perceptual similarity metric (SSIM on luminance, 8×8 blocks, C1/C2
#   constants from the original Wang-Bovik paper).
#   PASS if SSIM ≥ 0.98 (near-lossless perceptual match).
#   This answers: "Did a different GPU/driver produce a visually equivalent
#   image even though the pixels aren't identical?"
#
# Output: stdout reports EXACT_SHA:PASS/FAIL + VISUAL_SCORE:PASS/FAIL.
# On FAIL, artifacts are written to <output_dir>/:
#   actual.png     — the rendered image
#   reference.png  — the golden reference
#   diff.png       — visual diff (red = different pixel)
#   score.json     — structured metrics
#
# Dependencies: Pillow (PIL), ImageMagick ('convert' on PATH for diff image),
#               stdlib only (hashlib, json, math, subprocess, argparse).
#               ZERO numpy, ZERO scikit-image, ZERO OpenCV.
# ---------------------------------------------------------------------------

import argparse
import hashlib
import json
import math
import os
import struct
import subprocess
import sys

from PIL import Image, ImageOps


# ── Tier 1: Exact SHA-256 ────────────────────────────────────────────────

def compute_rgba_sha256(path: str) -> str:
    """
    SHA-256 of the raw RGBA pixel bytes after stripping headers/metadata.
    Uses ImageMagick 'convert' to produce a raw RGBA stream, then hashes it.
    This is the same approach as tools/check_determinism.sh.
    """
    proc = subprocess.run(
        ["convert", path, "rgba:-"],
        capture_output=True,
        check=True,
    )
    return hashlib.sha256(proc.stdout).hexdigest()


# ── Tier 2: Pure-Python SSIM (luminance only, 8×8 blocks) ────────────────

# SSIM constants from Wang et al. (2004), standard for 8-bit [0,255].
_C1 = (0.01 * 255) ** 2
_C2 = (0.03 * 255) ** 2


def _luminance(img: Image.Image) -> list[int]:
    """Extract luminance channel (Rec.601) as a flat list of ints [0,255]."""
    w, h = img.size
    lum = [0] * (w * h)
    pixels = list(img.convert("RGB").getdata())
    for i, (r, g, b) in enumerate(pixels):
        lum[i] = int(0.2989 * r + 0.5870 * g + 0.1140 * b)
    return lum


def _block_means(lum: list[int], w: int, h: int, block_size: int = 8
                 ) -> tuple[list[float], list[float]]:
    """Mean and variance of every 8×8 block in the image."""
    blocks_x = w // block_size
    blocks_y = h // block_size
    n = block_size * block_size
    means = []
    variances = []

    for by in range(blocks_y):
        for bx in range(blocks_x):
            vals = []
            for dy in range(block_size):
                row = by * block_size + dy
                off = row * w + bx * block_size
                vals.extend(lum[off:off + block_size])
            mu = sum(vals) / n
            var = sum((v - mu) ** 2 for v in vals) / (n - 1) if n > 1 else 0.0
            means.append(mu)
            variances.append(var)

    return means, variances


def _block_covariance(
    lum_a: list[int], lum_b: list[int],
    mean_a: list[float], mean_b: list[float],
    w: int, h: int, block_size: int = 8
) -> list[float]:
    """Covariance per 8×8 block between two images."""
    blocks_x = w // block_size
    blocks_y = h // block_size
    n = block_size * block_size
    covs = []
    idx = 0

    for by in range(blocks_y):
        for bx in range(blocks_x):
            cross = 0.0
            for dy in range(block_size):
                row = by * block_size + dy
                off = row * w + bx * block_size
                for dx in range(block_size):
                    cross += (lum_a[off + dx] - mean_a[idx]) * (
                        lum_b[off + dx] - mean_b[idx])
            covs.append(cross / (n - 1) if n > 1 else 0.0)
            idx += 1

    return covs


def compute_ssim(path_a: str, path_b: str, block_size: int = 8) -> float:
    """
    Pure-Python SSIM on luminance, 8×8 blocks, standard C1/C2 constants.
    Returns a float in [0.0, 1.0] where 1.0 = identical.
    """
    img_a = Image.open(path_a)
    img_b = Image.open(path_b)

    if img_a.size != img_b.size:
        # Resize to match (use reference dimensions)
        img_b = img_b.resize(img_a.size, Image.LANCZOS)

    w, h = img_a.size
    if w < block_size or h < block_size:
        # Image too small — fall back to mean absolute error
        lum_a = _luminance(img_a)
        lum_b = _luminance(img_b)
        mae = sum(abs(a - b) for a, b in zip(lum_a, lum_b)) / len(lum_a)
        return max(0.0, 1.0 - mae / 255.0)

    lum_a = _luminance(img_a)
    lum_b = _luminance(img_b)

    mean_a, var_a = _block_means(lum_a, w, h, block_size)
    mean_b, var_b = _block_means(lum_b, w, h, block_size)
    cov_ab = _block_covariance(lum_a, lum_b, mean_a, mean_b, w, h, block_size)

    ssim_vals = []
    for mu_a, mu_b, v_a, v_b, cov in zip(mean_a, mean_b, var_a, var_b,
                                           cov_ab):
        num = (2.0 * mu_a * mu_b + _C1) * (2.0 * cov + _C2)
        den = (mu_a ** 2 + mu_b ** 2 + _C1) * (v_a + v_b + _C2)
        ssim_vals.append(num / den if den > 0 else 1.0)

    return sum(ssim_vals) / len(ssim_vals) if ssim_vals else 0.0


# ── Tier 2: Per-pixel diff metrics (Pillow-only) ─────────────────────────

def compute_pixel_metrics(path_a: str, path_b: str) -> dict:
    """Mean absolute error and max pixel error."""
    img_a = Image.open(path_a).convert("RGBA")
    img_b = Image.open(path_b).convert("RGBA")

    if img_a.size != img_b.size:
        img_b = img_b.resize(img_a.size, Image.LANCZOS)

    w, h = img_a.size
    data_a = list(img_a.getdata())
    data_b = list(img_b.getdata())

    total_err = 0.0
    max_err = 0.0
    changed_pixels = 0
    total_pixels = w * h

    for pa, pb in zip(data_a, data_b):
        err = sum(abs(a - b) for a, b in zip(pa, pb)) / 4.0
        total_err += err
        if err > 0:
            changed_pixels += 1
        if err > max_err:
            max_err = err

    return {
        "mean_abs_error": round(total_err / total_pixels, 4) if total_pixels
                          else 0.0,
        "max_abs_error": round(max_err, 2),
        "changed_pixel_pct": round(changed_pixels / total_pixels * 100, 4)
                             if total_pixels else 0.0,
    }


# ── Diff image generation (via ImageMagick) ──────────────────────────────

def create_diff_image(actual_path: str, reference_path: str,
                      output_path: str) -> None:
    """
    Generate a visual diff image: matching pixels → grey, differing → red.
    Uses ImageMagick 'compare' which highlights differences.
    """
    subprocess.run([
        "convert", "(", actual_path, "-flatten", ")",
        "(", reference_path, "-flatten", ")",
        "-compose", "difference", "-composite",
        "-auto-level",
        output_path,
    ], check=True, capture_output=True)


# ── Main gate ─────────────────────────────────────────────────────────────

def run_gate(actual: str, reference: str, output_dir: str,
             ssim_threshold: float = 0.98) -> dict:
    os.makedirs(output_dir, exist_ok=True)

    result = {
        "actual": actual,
        "reference": reference,
        "EXACT_SHA": "NOT RUN",
        "VISUAL_SCORE": "NOT RUN",
        "details": {},
    }

    # ── Tier 1: Exact SHA-256 ──────────────────────────────────────────
    sha_actual = compute_rgba_sha256(actual)
    sha_ref = compute_rgba_sha256(reference)

    result["details"]["sha256_actual"] = sha_actual
    result["details"]["sha256_reference"] = sha_ref

    if sha_actual == sha_ref:
        result["EXACT_SHA"] = "PASS"
    else:
        result["EXACT_SHA"] = "FAIL"

    # ── Tier 2: Perceptual score ───────────────────────────────────────
    ssim = compute_ssim(actual, reference)
    pixel = compute_pixel_metrics(actual, reference)

    result["details"]["ssim"] = round(ssim, 6)
    result["details"]["pixel_metrics"] = pixel

    if ssim >= ssim_threshold:
        result["VISUAL_SCORE"] = "PASS"
    else:
        result["VISUAL_SCORE"] = "FAIL"

    # ── Artifacts on FAIL ─────────────────────────────────────────────
    exact_fail = result["EXACT_SHA"] != "PASS"
    visual_fail = result["VISUAL_SCORE"] != "PASS"

    if exact_fail or visual_fail:
        # Copy inputs for archival
        subprocess.run(["cp", actual, os.path.join(output_dir,
                                                    "actual.png")],
                       check=True)
        subprocess.run(["cp", reference, os.path.join(output_dir,
                                                       "reference.png")],
                       check=True)

        # Visual diff
        diff_png = os.path.join(output_dir, "diff.png")
        try:
            create_diff_image(actual, reference, diff_png)
            result["artifacts"] = {
                "actual": os.path.join(output_dir, "actual.png"),
                "reference": os.path.join(output_dir, "reference.png"),
                "diff": diff_png,
            }
        except (subprocess.CalledProcessError, FileNotFoundError):
            result["artifacts"] = {
                "actual": os.path.join(output_dir, "actual.png"),
                "reference": os.path.join(output_dir, "reference.png"),
                "diff": "(ImageMagick not available)",
            }

        # Write score.json
        score_path = os.path.join(output_dir, "score.json")
        with open(score_path, "w") as f:
            json.dump(result, f, indent=2)
        result["artifacts"]["score"] = score_path

    return result


def main() -> int:
    p = argparse.ArgumentParser(
        description="Chronon3D Two-Tier Visual Regression Gate")
    p.add_argument("--actual", required=True,
                   help="Rendered PNG to validate")
    p.add_argument("--reference", required=True,
                   help="Golden/baseline PNG")
    p.add_argument("--output-dir", default="test_renders/visual_gate",
                   help="Artifact directory for diff/score files")
    p.add_argument("--ssim-threshold", type=float, default=0.98,
                   help="SSIM threshold for VISUAL_SCORE PASS (default 0.98)")
    args = p.parse_args()

    for path, label in [(args.actual, "actual"), (args.reference,
                                                   "reference")]:
        if not path:
            # CI skip: no actual/reference configured for this run.
            print(f"[SKIP] {label} path not set — nothing to compare",
                  file=sys.stderr)
            return 2
        if not os.path.isfile(path):
            print(f"ERROR: {label} file not found: {path}", file=sys.stderr)
            return 2

    result = run_gate(args.actual, args.reference, args.output_dir,
                      args.ssim_threshold)

    # ── Gate output (parseable by CI) ─────────────────────────────────
    print(f"EXACT_SHA:    {result['EXACT_SHA']}")
    print(f"VISUAL_SCORE: {result['VISUAL_SCORE']}")
    print(f"  sha256 actual:    {result['details']['sha256_actual'][:16]}...")
    print(f"  sha256 reference: {result['details']['sha256_reference'][:16]}...")
    print(f"  ssim:  {result['details']['ssim']:.6f}")
    pmet = result['details']['pixel_metrics']
    print(f"  mae:   {pmet['mean_abs_error']:.4f}")
    print(f"  max:   {pmet['max_abs_error']:.2f}")
    print(f"  chg%:  {pmet['changed_pixel_pct']:.2f}%")

    if result.get("artifacts"):
        print(f"\nArtifacts saved to {args.output_dir}/:")
        for name, path in result["artifacts"].items():
            print(f"  {name}: {path}")

    # ── Exit code ─────────────────────────────────────────────────────
    # 0 = all PASS, 1 = at least one FAIL, 2 = error
    if result["EXACT_SHA"] == "NOT RUN" or result["VISUAL_SCORE"] == "NOT RUN":
        return 2
    if result["EXACT_SHA"] == "FAIL" or result["VISUAL_SCORE"] == "FAIL":
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())