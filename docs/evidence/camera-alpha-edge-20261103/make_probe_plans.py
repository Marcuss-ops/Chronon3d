#!/usr/bin/env python3
"""Derive the alpha-edge probe plans from the frozen v3 acceptance plan.

The probes differ from the frozen plan ONLY in the DoF switch and, for the
matte probe, in one layer opacity:

  probe-dof-off.plan.json            -> camera.dof.enabled = false (full scene)
  probe-dof-off-alpha-off.plan.json  -> same, plus layer "4" (the foreground
                                       element with fractional alpha) at
                                       opacity 0, which yields the matte
                                       M = |full - without_element| in
                                       engine space, without reimplementing
                                       the projection.
  diagnostic-dof-extreme.plan.json   -> DoF on with focus_distance=100 and
                                       max_blur=200 as a lane diagnostic.

The DoF switch MUST be written to every camera sample, not only to the
top-level `camera`: when `camera_samples` is present the runtime evaluates the
camera from the samples, so a top-level-only edit leaves DoF enabled and makes
the "off" probe byte-identical to the "on" one. That was the defect in the
first version of this harness (2026-11-03).

Nothing else is touched: layer transforms, shutter samples, canvas and output
are copied verbatim so the matte is measured on the acceptance fixture.
"""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT.parent / "camera-closure-final" / "optical_current_pair_v3.plan.json"
ALPHA_LAYER = "4"


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    plan = json.loads(SOURCE.read_text())
    layers = [layer["id"] for layer in plan["layers"]]
    if ALPHA_LAYER not in layers:
        raise SystemExit(f"layer {ALPHA_LAYER} absent from {SOURCE}")

    def set_dof(payload, *, enabled, focus_distance=None, max_blur=None):
        cameras = [payload["camera"]]
        cameras.extend(
            sample["camera"] for sample in payload.get("camera_samples", [])
        )
        for camera in cameras:
            camera["dof"]["enabled"] = enabled
            if focus_distance is not None:
                camera["dof"]["focus_distance"] = focus_distance
            if max_blur is not None:
                camera["dof"]["max_blur"] = max_blur

    dof_off = json.loads(json.dumps(plan))
    set_dof(dof_off, enabled=False)

    extreme = json.loads(json.dumps(plan))
    set_dof(extreme, enabled=True, focus_distance=100.0, max_blur=200.0)

    alpha_off = json.loads(json.dumps(dof_off))
    for layer in alpha_off["layers"]:
        if layer["id"] != ALPHA_LAYER:
            continue
        layer["opacity"] = 0
        for sample in layer.get("transform_samples", []):
            sample["opacity"] = 0

    for name, payload in (
        ("probe-dof-off.plan.json", dof_off),
        ("probe-dof-off-alpha-off.plan.json", alpha_off),
        ("diagnostic-dof-extreme.plan.json", extreme),
    ):
        (ROOT / name).write_text(json.dumps(payload, indent=2) + "\n")

    manifest = {
        "source_plan": str(SOURCE.relative_to(ROOT.parent.parent.parent)),
        "source_plan_sha256": sha256(SOURCE),
        "alpha_layer": ALPHA_LAYER,
        "alpha_layer_asset": next(
            layer["asset"] for layer in plan["layers"] if layer["id"] == ALPHA_LAYER
        ),
        "probes": {
            name: {
                "sha256": sha256(ROOT / name),
                "difference_from_source": difference,
            }
            for name, difference in (
                (
                    "probe-dof-off.plan.json",
                    "camera.dof.enabled=false in camera and all camera_samples",
                ),
                (
                    "probe-dof-off-alpha-off.plan.json",
                    "camera.dof.enabled=false in camera and all camera_samples; "
                    "layer 4 opacity=0 (all transform samples)",
                ),
                (
                    "diagnostic-dof-extreme.plan.json",
                    "camera.dof focus_distance=100 and max_blur=200 in camera "
                    "and all camera_samples",
                ),
            )
        },
        "frozen_acceptance_plan_sha256": "c2467b44fed0f0d007af7b3e38a3dd57e13728295b506701d7d3c02306e376d6",
    }
    (ROOT / "probe-plans.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
