"""The 00 vocabulary probe: one layer per primitive, minimal per layer.

This is the plan the README's capability table measures.  It answers "which
primitives are live in this build" before a scene is authored, so it stays
deliberately sparse: one gradient rect, one tilted 3D disc, one stroked bezier
with a write-on dash, one `follow_path` head, one `dot_grid`, three
`grid_layout` badges and one `${variable}` counter.
"""

from __future__ import annotations

from plan_lib import (
    FRAMES, W, H, at, bezier_commands, cubic_length, keyframe, linear_gradient,
    plan, rgba, shape_layer, text_layer, track,
)

P0, P1, P2, P3 = (-260.0, 60.0), (-120.0, -70.0), (60.0, 90.0), (260.0, 60.0)


def build() -> dict:
    layers = []

    layers.append(shape_layer(
        "bg_gradient",
        {
            "type": "rect",
            "fill": linear_gradient(
                (0.0, 0.0), (1.0, 0.0),
                [(0.0, "#071026", 1.0), (0.55, "#141026", 1.0), (1.0, "#2a0812", 1.0)],
            ),
        },
        (W, H), at(0, 0),
        background_look={"type": "film_grade", "grain": 0.05, "seed": 7},
    ))

    # A disc, not an ellipse: the only 2.5D depth the layer contract offers is a
    # per-layer Euler rotation, so the ring is a flat ellipse tilted on X.
    layers.append(shape_layer(
        "tilted_ring",
        {
            "type": "ellipse",
            "fill": linear_gradient(
                (0.0, 0.0), (0.0, 1.0),
                [(0.0, "#ff6a4d", 1.0), (1.0, "#2f4bd6", 1.0)],
            ),
        },
        (340, 340), at(-240, 60),
        enable_3d=True, rotation=[58, 0, 0], scale=[1.0, 0.35, 1.0],
        depth_role="midground",
        animation={"tracks": [
            track("position", [keyframe(0, [-240, -20, 0]), keyframe(24, [-240, 60, 0])],
                  easing="out_back"),
        ]},
    ))

    ribbon = bezier_commands(P0, P1, P2, P3)
    length = cubic_length(P0, P1, P2, P3)

    # Write-on: a full-period offset hides the dash completely, so animating the
    # offset to zero draws the stroke along its own length.
    layers.append(shape_layer(
        "flow_ribbon",
        {
            "type": "path",
            "path": ribbon,
            "stroke": {
                "color": "#FFFFFF",
                "width": 6,
                "dash_array": [length, length],
                "dash_offset": length,
            },
        },
        (W, H), at(0, 150),
        animation={"tracks": [
            track("stroke_dash_offset",
                  [keyframe(6, length), keyframe(42, 0.0)], easing="in_out_cubic"),
        ]},
    ))

    layers.append(shape_layer(
        "ribbon_head",
        {"type": "ellipse", "fill": rgba("#FFFFFF")},
        (18, 18), at(0, 150),
        follow_path={
            "path_commands": ribbon,
            "progress": {"keyframes": [
                {"frame": 6, "value": 0.0},
                {"frame": 42, "value": 1.0},
            ]},
        },
    ))

    layers.append(shape_layer(
        "social_dots",
        {"type": "dot_grid", "fill": rgba("#3a4356"), "spacing": 26, "dot_radius": 6},
        (300, 130), at(0, -170),
    ))

    for index, (_label, x) in enumerate((("TikTok", -260), ("Instagram", 0), ("YouTube", 260))):
        layers.append(shape_layer(
            "badge_%d" % index,
            {"type": "rounded_rect", "corner_radius": [14, 14, 14, 14], "fill": rgba("#1b2233")},
            (220, 56), at(x, 250),
            grid_layout={"group_id": "badges", "columns": 3, "gap": [20, 0]},
        ))

    layers.append(text_layer(
        "counter", "Clicks: ${clicks}", (520, 60), at(240, -60), 40.0, "#F4F7FF",
    ))

    return plan(
        "infographic_2p5d_00_vocabulary_probe",
        layers,
        variables={"clicks": 816},
        frames=FRAMES,
    )
