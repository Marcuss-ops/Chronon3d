#!/usr/bin/env python3
"""Generate the 2.5D infographic render-plan fixtures.

These are executable probes for the *current* render-plan contract, in the spirit
of `text_3d_stress`: every layer is built from a primitive the contract actually
exposes, and the README records what a render really shows.  Measurements come
from decoding the rendered frames (`measure.py`), not from reading the schema.

Each `scene_*.py` module owns one plan family and exposes `build()`; this script
is only the registry and the writer.

Usage: python3 generate.py [out-dir]
"""

from __future__ import annotations

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import scene_camera_regressions  # noqa: E402
import scene_infographic  # noqa: E402
import scene_probe  # noqa: E402

# Written in this order so the cheapest self-check (the vocabulary probe) lands
# first and the measured acceptance plans are easy to find.
SCENES = (
    ("00_vocabulary_probe", scene_probe.build),
    ("10_regression_2d_reference", scene_camera_regressions.reference),
    ("11_regression_2d_under_3d", scene_camera_regressions.under_3d),
    ("12_regression_2d_under_implicit", scene_camera_regressions.under_implicit_camera),
    ("13_regression_2d_plus_color_3d", scene_camera_regressions.under_color_3d),
    ("20_scene", scene_infographic.build),
)


def main() -> int:
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "plans")
    os.makedirs(out, exist_ok=True)
    written = []
    for name, builder in SCENES:
        plan = builder()
        path = os.path.join(out, name + ".plan.json")
        with open(path, "w") as handle:
            json.dump(plan, handle, indent=2)
            handle.write("\n")
        written.append((name, len(plan["layers"]), bool(plan.get("camera"))))
    for name, count, camera in written:
        print("infographic_2p5d: wrote %s.plan.json (%d layers%s)"
              % (name, count, ", camera" if camera else ""))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
