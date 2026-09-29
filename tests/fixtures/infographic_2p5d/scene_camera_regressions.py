"""Minimal, measurable reproductions for the closed 2.5D camera defects.

Defect (closed): adding a layer with `enable_3d` — or declaring a camera — made
every 2D shape in the same composition vanish.  A resolved layer matrix is
expressed in canvas pixels, while the camera homography is authored in the
canonical centre-relative y-up basis and composes its own canvas-centre prefix;
feeding the pixel matrix straight in applied the centre twice *and* mirrored y,
which pushed 2D geometry off-canvas.  A bezier ribbon authored at canvas
[480, 420] on 960x540 projected to (757.9, 26.8) instead of staying put.

These four plans are the rerunnable form of that bisection.  They share one
ribbon so their measured ink is directly comparable:

  10_regression_2d_reference         ribbon alone, no camera, no 3D layer
  11_regression_2d_under_3d          ribbon + tilted 3D ring + explicit camera
  12_regression_2d_under_implicit    ribbon + tilted 3D ring, implicit camera
  13_regression_2d_plus_color_3d     ribbon + `color` layer with enable_3d

Acceptance, measured with `measure.py`:

  * 11 and 13 must report the *same white-bbox as 10* — with a head-on camera
    whose focal length matches the camera distance, a layer in the z=0 plane is
    left untouched, so a present camera may not move the 2D layout at all;
  * 12 must report non-zero white ink (the implicit camera does rescale the
    plane about the canvas centre, so its bbox is smaller, but never empty).
"""

from __future__ import annotations

from plan_lib import (
    W, at, bezier_commands, cubic_length, keyframe, linear_gradient, one_to_one_camera,
    plan, rgba, shape_layer, track,
)

# The same hero bezier the vocabulary probe and the full scene use.
P0, P1, P2, P3 = (-260.0, 60.0), (-120.0, -70.0), (60.0, 90.0), (260.0, 60.0)
RIBBON_Y = 150.0

RIBBON_WIDTH = 6


def _ribbon_layer(animated: bool = True) -> dict:
    commands = bezier_commands(P0, P1, P2, P3)
    length = cubic_length(P0, P1, P2, P3)
    extra = {}
    if animated:
        extra["animation"] = {"tracks": [
            track("stroke_dash_offset",
                  [keyframe(6, length), keyframe(42, 0.0)], easing="in_out_cubic"),
        ]}
    return shape_layer(
        "flow_ribbon",
        {
            "type": "path",
            "path": commands,
            "stroke": {
                "color": "#FFFFFF",
                "width": RIBBON_WIDTH,
                "dash_array": [length, length],
                "dash_offset": 0.0 if not animated else length,
            },
        },
        (W, 540), at(0, RIBBON_Y),
        **extra,
    )


def _tilted_ring(layer_id: str = "tilted_ring") -> dict:
    """The smallest `enable_3d` layer that used to erase the ribbon.

    `enable_3d` layers author in the camera basis (origin at the canvas centre,
    +y up), so `at(280, 190)` is what lands at canvas (760, 80).
    """
    return shape_layer(
        layer_id,
        {
            "type": "ellipse",
            "fill": linear_gradient(
                (0.0, 0.0), (0.0, 1.0),
                [(0.0, "#ff6a4d", 1.0), (1.0, "#2f4bd6", 1.0)],
            ),
        },
        (300, 300), at(280, 190),
        enable_3d=True, rotation=[58, 0, 0], scale=[1.0, 0.35, 1.0],
    )


def reference() -> dict:
    """No camera and no 3D layer: the pixels every other plan is compared to."""
    return plan("infographic_2p5d_10_regression_2d_reference",
                [_ribbon_layer(animated=False)])


def under_3d() -> dict:
    """A 3D layer plus a camera that leaves z=0 at 1:1."""
    return plan("infographic_2p5d_11_regression_2d_under_3d",
                [_tilted_ring(), _ribbon_layer(animated=False)],
                camera=one_to_one_camera())


def under_implicit_camera() -> dict:
    """A 3D layer with no camera block: the compiler derives one."""
    return plan("infographic_2p5d_12_regression_2d_under_implicit",
                [_tilted_ring(), _ribbon_layer(animated=False)])


def under_color_3d() -> dict:
    """The `color` layer variant: `enable_3d` on a non-shape layer."""
    plate = {
        "id": "plate3d",
        "type": "color",
        "size": [200, 120],
        "position": list(at(-280, -170)),
        "start_frame": 0,
        "duration_frames": 60,
        "color": rgba("#2b3550"),
        "enable_3d": True,
        "rotation": [40, 0, 0],
    }
    return plan("infographic_2p5d_13_regression_2d_plus_color_3d",
                [plate, _ribbon_layer(animated=False)],
                camera=one_to_one_camera())
