#!/usr/bin/env python3
"""Measure the preregistered alpha-edge gates on the frozen Optical Studio v3 pair.

Reads the three 1920x1080 RGBA8 readbacks of each sentinel frame:

  A  probe-dof-off-<f>.raw.rgba            DoF off, full scene
  B  probe-dof-off-alpha-off-<f>.raw.rgba  DoF off, alpha layer at opacity 0
  C  probe-dof-on-<f>.raw.rgba             DoF on, frozen acceptance plan

and evaluates exactly the gates frozen in measurement-contract.json. The
contract digest is written into the result, so editing the contract afterwards
invalidates the evidence instead of moving the threshold.

The DoF switch must be written to `camera` AND to every `camera_samples[*]`:
when samples are present the runtime evaluates the camera from them, so a
top-level-only edit leaves DoF enabled and makes A equal to C.
"""
import hashlib
import json
import subprocess
from pathlib import Path

import numpy as np
from scipy.ndimage import binary_dilation, binary_erosion, maximum_filter, minimum_filter

ROOT = Path(__file__).resolve().parent
WIDTH, HEIGHT = 1920, 1080
BAND_DILATION = 2
MATTE_SUPPORT = 8
HALO_RADIUS = 28  # camera.dof.max_blur
HALO_TOLERANCE = 8
HUE_TOLERANCE = 16


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load(path):
    data = np.fromfile(path, dtype=np.uint8)
    expected = WIDTH * HEIGHT * 4
    if data.size != expected:
        raise ValueError(f"{path}: expected {expected} RGBA bytes, got {data.size}")
    return data.reshape(HEIGHT, WIDTH, 4)


def validation_errors(path):
    return path.read_text(errors="replace").count("Vulkan Validation ERROR")


def measure_frame(frame):
    a_path = ROOT / f"probe-dof-off-{frame:03}.raw.rgba"
    b_path = ROOT / f"probe-dof-off-alpha-off-{frame:03}.raw.rgba"
    c_path = ROOT / f"probe-dof-on-{frame:03}.raw.rgba"

    a = load(a_path)[:, :, :3].astype(np.int16)
    b = load(b_path)[:, :, :3].astype(np.int16)
    c = load(c_path)[:, :, :3].astype(np.int16)

    # Validity precondition: the whole measurement only means something if the
    # DoF-on readback actually differs from the DoF-off one. A byte-identical
    # pair would make every halo/contamination gate vacuously pass, so it is
    # reported as BLOCKED instead of PASS.
    dof_delta = np.abs(a - c)
    dof_observable = bool(dof_delta.any())

    matte = np.abs(a - b).max(axis=2)
    support = matte >= MATTE_SUPPORT
    if not support.any():
        raise ValueError(f"frame {frame}: empty matte — the alpha probe rendered nothing")

    boundary = support & ~binary_erosion(support)
    band = binary_dilation(boundary, iterations=BAND_DILATION)
    band &= binary_dilation(support, iterations=BAND_DILATION) | boundary

    p99 = float(np.percentile(matte[support], 99))
    q = np.clip(matte / max(p99, 1.0), 0.0, 1.0)
    fractional = int(((q >= 0.05) & (q <= 0.95) & band).sum())

    # G2: a separable Gaussian blur is a convex combination of neighbours, so the
    # blurred frame cannot leave the per-channel range of the sharp frame inside
    # the blur footprint.
    def overshoot_at(radius):
        window = 2 * radius + 1
        hi_local = np.stack(
            [maximum_filter(a[:, :, ch], size=window, mode="nearest") for ch in range(3)],
            axis=2,
        )
        lo_local = np.stack(
            [minimum_filter(a[:, :, ch], size=window, mode="nearest") for ch in range(3)],
            axis=2,
        )
        local = np.clip(np.maximum(c - hi_local, lo_local - c), 0, None)
        inside = local[band]
        return {
            "radius_px": radius,
            "violating_pixels": int((inside > HALO_TOLERANCE).any(axis=1).sum()),
            "max_overshoot": int(inside.max()) if inside.size else 0,
        }

    sensitivity = [overshoot_at(radius) for radius in (HALO_RADIUS, 48, 80)]
    frozen = sensitivity[0]
    violating = frozen["violating_pixels"]
    band_pixels = int(band.sum())
    max_overshoot = frozen["max_overshoot"]

    # G3: hue flip — one channel pushed up while another is pulled down beyond
    # the tolerance, the signature of the green/rainbow boundary flashes.
    residual = c - a
    contaminated = (
        (residual.max(axis=2) > HUE_TOLERANCE) & (residual.min(axis=2) < -HUE_TOLERANCE)
    )
    contaminated_pixels = int((contaminated & band).sum())

    residual_band = np.abs(residual)[band]
    ys, xs = np.nonzero(support)

    return {
        "frame": frame,
        "inputs": {
            "dof_off_full": {"path": str(a_path.relative_to(ROOT.parent.parent.parent)), "sha256": sha256(a_path)},
            "dof_off_alpha_off": {"path": str(b_path.relative_to(ROOT.parent.parent.parent)), "sha256": sha256(b_path)},
            "dof_on": {"path": str(c_path.relative_to(ROOT.parent.parent.parent)), "sha256": sha256(c_path)},
        },
        "matte": {
            "support_pixels": int(support.sum()),
            "p99": p99,
            "bbox_xyxy": [int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())],
            "centroid": [float(xs.mean()), float(ys.mean())],
        },
        "validity": {
            "dof_observable": dof_observable,
            "dof_on_vs_off_mean_abs_delta": float(dof_delta.mean()),
            "dof_on_vs_off_max_abs_delta": int(dof_delta.max()),
        },
        "edge_band": {
            "pixels": band_pixels,
            "fractional_coverage_pixels": fractional,
            "mean_abs_residual": float(residual_band.mean()) if residual_band.size else 0.0,
        },
        "G1_semi_transparent_edge_present": {
            "value": fractional,
            "threshold": ">= 100 px",
            "gate": "PASS" if fractional >= 100 else "FAIL",
        },
        "G2_no_halo_overshoot": {
            "violating_pixels": violating,
            "violating_ratio": violating / max(band_pixels, 1),
            "max_overshoot": max_overshoot,
            "threshold": "ratio <= 0.002 and max_overshoot <= 8",
            "gate": "PASS"
            if violating / max(band_pixels, 1) <= 0.002 and max_overshoot <= HALO_TOLERANCE
            else "FAIL",
        },
        "G3_no_hue_contamination": {
            "contaminated_pixels": contaminated_pixels,
            "contaminated_ratio": contaminated_pixels / max(band_pixels, 1),
            "threshold": "ratio <= 0.005",
            "gate": "PASS" if contaminated_pixels / max(band_pixels, 1) <= 0.005 else "FAIL",
        },
        "diagnostic_blur_footprint_sensitivity": sensitivity,
        "diagnostic_blur_footprint_note": "not a gate: shows whether an overshoot survives when the legitimate blur footprint is widened beyond camera.dof.max_blur. An overshoot that disappears at a larger radius means the frozen window was too small to separate halo from legitimate spread; it does NOT turn the frozen FAIL into a PASS.",
        "vulkan_validation_errors": {
            "dof_off_full": validation_errors(ROOT / f"probe-dof-off-{frame:03}.log"),
            "dof_off_alpha_off": validation_errors(ROOT / f"probe-dof-off-alpha-off-{frame:03}.log"),
            "dof_on": validation_errors(ROOT / f"probe-dof-on-{frame:03}.log"),
        },
    }


def git(repo, *args):
    return subprocess.run(
        ["git", "-C", str(repo), *args], capture_output=True, text=True, check=True
    ).stdout.strip()


def main():
    frames = [30, 75, 149]
    results = [measure_frame(frame) for frame in frames]

    centroid_30 = np.array(results[0]["matte"]["centroid"])
    centroid_149 = np.array(results[-1]["matte"]["centroid"])
    displacement = float(np.linalg.norm(centroid_149 - centroid_30))
    area_30 = results[0]["matte"]["support_pixels"]
    area_149 = results[-1]["matte"]["support_pixels"]
    area_ratio = area_149 / area_30
    inside_canvas = all(
        0 <= r["matte"]["bbox_xyxy"][0]
        and r["matte"]["bbox_xyxy"][1] >= 0
        and r["matte"]["bbox_xyxy"][2] < WIDTH
        and r["matte"]["bbox_xyxy"][3] < HEIGHT
        for r in results
    )
    parallax_gate = (
        "PASS"
        if displacement >= 1.0 and 0.5 <= area_ratio <= 2.0 and inside_canvas
        else "FAIL"
    )

    gates = [
        r["G1_semi_transparent_edge_present"]["gate"] for r in results
    ] + [
        r["G2_no_halo_overshoot"]["gate"] for r in results
    ] + [
        r["G3_no_hue_contamination"]["gate"] for r in results
    ] + [parallax_gate]
    validation = sum(
        sum(r["vulkan_validation_errors"].values()) for r in results
    )
    dof_observable = all(r["validity"]["dof_observable"] for r in results)
    if validation != 0:
        overall = "FAIL"
    elif not dof_observable:
        overall = "BLOCKED_DOF_INERT"
    elif all(g == "PASS" for g in gates):
        overall = "PASS"
    else:
        overall = "FAIL"

    chronon3d = ROOT.parent.parent.parent
    motion3d = chronon3d.parent / "ChrononMotion3D"
    dirty_diff = hashlib.sha256(
        (git(chronon3d, "diff") + git(motion3d, "diff")).encode()
    ).hexdigest()

    report = {
        "contract": {
            "path": "docs/evidence/camera-alpha-edge-20261103/measurement-contract.json",
            "sha256": sha256(ROOT / "measurement-contract.json"),
        },
        "probes": json.loads((ROOT / "probe-plans.json").read_text()),
        "comparison": "DoF-on native Vulkan readback (C) against the DoF-off sharp readback of the same frozen plan (A), inside the alpha edge band derived from the engine-space matte A-B",
        "pair": {
            "Chronon3d_head": git(chronon3d, "rev-parse", "HEAD"),
            "Chronon3d_dirty": bool(git(chronon3d, "status", "--porcelain")),
            "ChrononMotion3D_head": git(motion3d, "rev-parse", "HEAD"),
            "ChrononMotion3D_dirty": bool(git(motion3d, "status", "--porcelain")),
            "combined_tracked_diff_sha256": dirty_diff,
            "binary": "Chronon3d/build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli",
            "binary_sha256": sha256(
                chronon3d / "build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli"
            ),
            "host_device": "NVIDIA RTX A4000, Vulkan validation layers enabled, raw RGBA8 readback",
            "same_sha": False,
            "same_sha_note": "both trees are dirty, so this is local dirty-tree evidence, not a CI same-SHA certification",
        },
        "frames": results,
        "G4_parallax_and_placement": {
            "centroid_displacement_px": displacement,
            "area_ratio_149_over_30": area_ratio,
            "support_inside_canvas": inside_canvas,
            "threshold": "displacement >= 1 px, area ratio in [0.5, 2.0], inside canvas",
            "gate": parallax_gate,
        },
        "vulkan_validation_errors_total": validation,
        "dof_observable_on_every_frame": dof_observable,
        "aggregate_gate": overall,
        "aggregate_explanation": (
            "G1-G4 are trivially satisfied when the DoF-on and DoF-off readbacks are "
            "byte-identical; that is not a certification of the alpha edge but a "
            "broken measurement lane, so the aggregate is BLOCKED, never PASS."
            if not dof_observable else "gates evaluated against a DoF-on readback that differs from the sharp one"
        ),
    }
    (ROOT / "alpha-edge-result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0 if overall == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
