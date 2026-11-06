#!/usr/bin/env python3
"""Compare the GEL debug SDF against a pixel-grid Euclidean distance oracle.

The oracle is derived independently from the flat TextRun raw frame. It is a
center-sampled signed Euclidean distance transform of the 0.5-coverage mask;
it does not claim subpixel vector-contour accuracy.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image
from scipy.ndimage import distance_transform_edt


def read_rgba(path: Path, width: int, height: int) -> np.ndarray:
    raw = np.fromfile(path, dtype=np.uint8)
    expected = width * height * 4
    if raw.size != expected:
        raise ValueError(f"{path}: expected {expected} RGBA bytes, got {raw.size}")
    return raw.reshape(height, width, 4)


def srgb_to_linear(value: np.ndarray) -> np.ndarray:
    encoded = value.astype(np.float32) / 255.0
    return np.where(encoded <= 0.04045, encoded / 12.92,
                    ((encoded + 0.055) / 1.055) ** 2.4)


def colorize_error(error: np.ndarray, valid: np.ndarray, edge: np.ndarray,
                   limit: float) -> np.ndarray:
    t = np.clip(error / limit, 0.0, 1.0)
    low = np.array([20.0, 145.0, 255.0], dtype=np.float32)
    high = np.array([255.0, 55.0, 20.0], dtype=np.float32)
    rgb = low[None, None, :] * (1.0 - t[..., None]) + high[None, None, :] * t[..., None]
    rgb[~valid] = np.array([7.0, 10.0, 18.0], dtype=np.float32)
    rgb[edge] = np.array([255.0, 255.0, 255.0], dtype=np.float32)
    return np.clip(rgb, 0.0, 255.0).astype(np.uint8)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mask-raw", type=Path, required=True,
                        help="T01 flat Vulkan/raw RGBA frame")
    parser.add_argument("--field-raw", type=Path, required=True,
                        help="T02 GEL debug mode 1 Vulkan/raw RGBA frame")
    parser.add_argument("--normal-raw", type=Path,
                        help="optional T04 GEL debug mode 3 normal-map frame")
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--radius-px", type=float, default=7.3125,
                        help="debug encoding radius (T02 90px Inter-Bold profile: 5.2*90/64)")
    parser.add_argument("--band-min", type=float, default=-3.0)
    parser.add_argument("--band-max", type=float, default=4.0)
    parser.add_argument("--error-image", type=Path, required=True)
    parser.add_argument("--metrics", type=Path, required=True)
    args = parser.parse_args()
    if args.radius_px <= 0 or args.band_min >= args.band_max:
        parser.error("radius must be positive and band-min must be less than band-max")

    flat = read_rgba(args.mask_raw, args.width, args.height)
    field = read_rgba(args.field_raw, args.width, args.height)
    flat_linear = srgb_to_linear(flat[:, :, :3])
    luminance = flat_linear @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
    backdrop_luma = float(np.median(luminance[:8, :8]))
    threshold = backdrop_luma + 0.5 * (1.0 - backdrop_luma)
    mask = luminance > threshold

    inside = distance_transform_edt(mask) - 0.5
    outside = -(distance_transform_edt(~mask) - 0.5)
    oracle = np.where(mask, inside, outside).astype(np.float32)

    # T02 writes encoded_distance into linear red before the raw sink converts
    # to premultiplied sRGB RGBA8. Decode sRGB before inverting that mapping.
    field_red_linear = srgb_to_linear(field[:, :, 0])
    decoded = (2.0 * field_red_linear - 1.0) * args.radius_px
    band = (oracle >= args.band_min) & (oracle <= args.band_max)
    error = np.abs(decoded - oracle)
    samples = error[band]
    if samples.size == 0:
        raise ValueError("oracle band contains no pixels")

    sign_mask = decoded > 0.0
    intersection = int(np.count_nonzero(mask & sign_mask))
    union = int(np.count_nonzero(mask | sign_mask))
    edge = np.zeros_like(mask)
    edge[1:-1, 1:-1] = mask[1:-1, 1:-1] != (
        mask[:-2, 1:-1] & mask[2:, 1:-1] &
        mask[1:-1, :-2] & mask[1:-1, 2:])
    image = colorize_error(error, band, edge, limit=1.5)

    metrics = {
        "oracle": "center-sampled signed Euclidean distance transform of T01 0.5-coverage mask; boundary estimate is pixel-grid, not subpixel vector contour",
        "mask_raw": args.mask_raw.name,
        "field_raw": args.field_raw.name,
        "dimensions": [args.width, args.height],
        "mask_threshold_linear_luma": threshold,
        "backdrop_luma_linear": backdrop_luma,
        "field_encoding": "T02 linear red = 0.5 + 0.5 * clamp(distance/radius,-1,1); raw sRGB decoded before inversion",
        "field_radius_px": args.radius_px,
        "radius_derivation": "5.2 * (font_size 90 / canonical MTSDF em_px 64), run scale 1",
        "comparison_band_px": [args.band_min, args.band_max],
        "sample_count": int(samples.size),
        "mae_px": float(np.mean(samples)),
        "median_abs_error_px": float(np.median(samples)),
        "p95_abs_error_px": float(np.percentile(samples, 95)),
        "max_abs_error_px": float(np.max(samples)),
        "within_0_5_px_fraction": float(np.mean(samples <= 0.5)),
        "within_1_px_fraction": float(np.mean(samples <= 1.0)),
        "within_1_5_px_fraction": float(np.mean(samples <= 1.5)),
        "signed_region_iou": float(intersection / union) if union else 1.0,
        "mask_pixels": int(np.count_nonzero(mask)),
        "debug_positive_pixels": int(np.count_nonzero(sign_mask)),
        "error_image": args.error_image.name,
        "status": "PASS_LIMITED" if np.percentile(samples, 95) <= 0.5 else "FAIL",
        "limitation": "The oracle measures the rasterized coverage boundary; it does not validate the original outline at subpixel precision or all scale/rotation conditions.",
    }
    if args.normal_raw:
        normal_frame = read_rgba(args.normal_raw, args.width, args.height)
        normal_color = srgb_to_linear(normal_frame[:, :, :2])
        actual_xy = 2.0 * normal_color - 1.0
        gradient_x = np.zeros_like(oracle)
        gradient_y = np.zeros_like(oracle)
        gradient_x[:, 2:-2] = (oracle[:, 4:] - oracle[:, :-4]) * 0.25
        gradient_y[2:-2, :] = (oracle[4:, :] - oracle[:-4, :]) * 0.25
        expected_xy = -np.stack((gradient_x, gradient_y), axis=-1)
        expected_length = np.linalg.norm(expected_xy, axis=-1)
        actual_length = np.linalg.norm(actual_xy, axis=-1)
        normal_band = (oracle >= 0.0) & (oracle <= 3.5)
        normal_band &= (expected_length >= 0.5) & (expected_length <= 1.5)
        normal_band &= actual_length >= 0.05
        cosine = np.sum(actual_xy * expected_xy, axis=-1) / np.maximum(
            actual_length * expected_length, 1e-6)
        cosine = np.clip(cosine[normal_band], -1.0, 1.0)
        if cosine.size == 0:
            raise ValueError("normal oracle band contains no samples")
        angle = np.degrees(np.arccos(cosine))
        metrics["normal_raw"] = args.normal_raw.name
        metrics["normal_oracle"] = "directional cosine between T04 XY normal and negative T01-oracle SDF gradient; shader-aligned four-pixel central stencil"
        metrics["normal_sample_count"] = int(cosine.size)
        metrics["normal_mean_cosine"] = float(np.mean(cosine))
        metrics["normal_p05_cosine"] = float(np.percentile(cosine, 5))
        metrics["normal_median_angle_deg"] = float(np.median(angle))
        metrics["normal_p95_angle_deg"] = float(np.percentile(angle, 95))
        metrics["normal_opposed_fraction"] = float(np.mean(cosine < 0.0))
        metrics["normal_limitation"] = "Checks XY direction in a central core band only; normal length/finite validity is covered by T04 mode 4, and this does not certify every scale or rotation."
    args.error_image.parent.mkdir(parents=True, exist_ok=True)
    args.metrics.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(image, mode="RGB").save(args.error_image)
    args.metrics.write_text(json.dumps(metrics, indent=2) + "\n")
    print(json.dumps(metrics, indent=2))
    return 0 if metrics["status"] == "PASS_LIMITED" else 1


if __name__ == "__main__":
    raise SystemExit(main())
