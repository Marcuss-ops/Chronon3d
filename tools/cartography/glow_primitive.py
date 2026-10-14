"""Shared map halo primitive matching Chronon3d's Glow pipeline.

Masks describe coverage, color is sRGB BGR, and compositing is additive light.
The blur is separable with the engine's sigma=radius/3 kernel and a 3-sigma
support. Filtering converts to linear light and premultiplies by coverage;
results are returned to premultiplied sRGB before additive composition.
"""
from __future__ import annotations

import math

import cv2
import numpy as np


def _srgb_to_linear(value: np.ndarray) -> np.ndarray:
    value = np.asarray(value, dtype=np.float32) / 255.0
    return np.where(value <= 0.04045, value / 12.92,
                    ((value + 0.055) / 1.055) ** 2.4)


def _linear_to_srgb(value: np.ndarray) -> np.ndarray:
    value = np.maximum(np.asarray(value, dtype=np.float32), 0.0)
    srgb = np.where(value <= 0.0031308, value * 12.92,
                    1.055 * np.power(value, 1.0 / 2.4) - 0.055)
    return np.clip(srgb * 255.0, 0.0, 255.0)


def gaussian_halo(mask: np.ndarray, color_bgr: tuple[int, int, int],
                  sigma: float, intensity: float = 1.0) -> np.ndarray:
    """Return a uint8 BGR halo from an 8-bit coverage mask.

    Alpha coverage is Gaussian-filtered separately. RGB is converted to
    linear sRGB, premultiplied by coverage, blurred, then unpremultiplied and
    encoded as premultiplied sRGB exactly as the engine glow kernel does.
    """
    coverage = np.asarray(mask)
    if coverage.ndim != 2 or coverage.dtype != np.uint8:
        raise ValueError("glow mask must be a 2D uint8 coverage image")
    if not math.isfinite(sigma) or sigma < 0.0:
        raise ValueError("glow sigma must be finite and non-negative")
    if not math.isfinite(intensity) or intensity < 0.0:
        raise ValueError("glow intensity must be finite and non-negative")
    color = np.asarray(color_bgr, dtype=np.float32)
    if color.shape != (3,) or not np.all(np.isfinite(color)):
        raise ValueError("glow color must contain three finite BGR channels")
    if sigma == 0.0 or intensity == 0.0 or not np.any(coverage):
        return np.zeros((*coverage.shape, 3), dtype=np.uint8)

    alpha = coverage.astype(np.float32) / 255.0 * intensity
    linear = _srgb_to_linear(np.clip(color, 0.0, 255.0))
    premultiplied = alpha[:, :, None] * linear[None, None, :]
    radius = max(1, math.ceil(sigma * 3.0))
    ksize = radius * 2 + 1
    filtered_alpha = cv2.GaussianBlur(alpha, (ksize, ksize), sigmaX=sigma,
                                      sigmaY=sigma, borderType=cv2.BORDER_CONSTANT)
    filtered_rgb = cv2.GaussianBlur(premultiplied, (ksize, ksize), sigmaX=sigma,
                                    sigmaY=sigma, borderType=cv2.BORDER_CONSTANT)
    # The engine clamps filtered alpha before converting back to premultiplied
    # sRGB. Unpremultiply in linear light before the transfer curve.
    out_alpha = np.clip(filtered_alpha, 0.0, 1.0)
    straight_linear = filtered_rgb / np.maximum(filtered_alpha[:, :, None], 1.0e-8)
    return np.clip(_linear_to_srgb(straight_linear) * out_alpha[:, :, None],
                   0.0, 255.0).astype(np.uint8)


def composite_halo(frame: np.ndarray, mask: np.ndarray,
                   color_bgr: tuple[int, int, int], sigma: float,
                   intensity: float = 1.0) -> np.ndarray:
    """Add the canonical halo to a uint8 BGR frame in place and return it."""
    if frame.dtype != np.uint8 or frame.ndim != 3 or frame.shape[2] != 3:
        raise ValueError("glow destination must be a uint8 BGR frame")
    if frame.shape[:2] != mask.shape:
        raise ValueError("glow mask dimensions must match destination frame")
    halo = gaussian_halo(mask, color_bgr, sigma, intensity)
    cv2.add(frame, halo, dst=frame)
    return frame
