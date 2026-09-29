"""The full 2.5D infographic scene.

Composition, canvas 960x540 (centre 480, 270), in draw order:

  band 1  canvas y   40..130   title + subtitle (left), tilted 3D ring (right)
  band 2  canvas y  165..335   funnel, four tapered stages (left)
                               three counter texts (middle column)
                               six-point step-line chart (right)
  band 3  canvas y  350..500   write-on bezier ribbon + `follow_path` head
  band 4  canvas y  478..522   three `grid_layout` badges

The camera is explicit and chosen so the z=0 plane renders 1:1
(`plan_lib.one_to_one_camera`): the tilted ring gets real perspective depth
while every 2D layer stays exactly where it was authored.  That is the property
the camera regressions in `scene_camera_regressions.py` pin.

Every element is a primitive the vocabulary probe measured as live; nothing here
needs a capability the build does not have.
"""

from __future__ import annotations

from plan_lib import (
    H, W, at, bezier_commands, cubic_length, fade_in, keyframe, linear_gradient,
    one_to_one_camera, plan, polyline_commands, rgba, shape_layer, step_commands,
    text_layer, track,
)

# ── Ribbon (hero) ───────────────────────────────────────────────────────────
P0, P1, P2, P3 = (-300.0, 70.0), (-140.0, -80.0), (70.0, 100.0), (300.0, 70.0)
RIBBON_CENTRE_Y = 140.0

# ── Funnel ──────────────────────────────────────────────────────────────────
FUNNEL_STAGE_W = 78.0
FUNNEL_PITCH = 88.0
FUNNEL_GROUP_CX = 170.0            # canvas x of the four-stage group centre
FUNNEL_GROUP_W = 4 * FUNNEL_STAGE_W + 3 * (FUNNEL_PITCH - FUNNEL_STAGE_W)
FUNNEL_CY = -20.0                  # canvas-centre-relative y (canvas y 250)

# (left edge height, right edge height) — every stage hands its right edge to the
# next stage's left edge, so the four trapezoids read as one funnel.
FUNNEL_SIZES = ((168.0, 132.0), (132.0, 96.0), (96.0, 62.0), (62.0, 34.0))
FUNNEL_COLOURS = (
    ("#ff6a4d", "#ffb35c"),
    ("#ff8a5c", "#7c5cff"),
    ("#7c5cff", "#38bdf8"),
    ("#38bdf8", "#2dd4bf"),
)

# ── Step-line chart ─────────────────────────────────────────────────────────
STEP_CX, STEP_CY = 250.0, -35.0    # canvas (730, 235)
STEP_XS = (-150.0, -90.0, -30.0, 30.0, 90.0, 150.0)
STEP_YS = (50.0, 10.0, 30.0, -20.0, -40.0, -70.0)
MARKER = 14.0

# ── Counters ────────────────────────────────────────────────────────────────
COUNTER_CX = -10.0                 # canvas x 470, between funnel and chart
COUNTER_YS = (-85.0, -20.0, 45.0)  # canvas y 185, 250, 315

BADGE_Y = 230.0                    # canvas y 500


def _funnel_layers() -> list:
    layers = []
    left = FUNNEL_GROUP_CX - FUNNEL_GROUP_W / 2.0
    for index, ((high, low), (top, bottom)) in enumerate(zip(FUNNEL_SIZES, FUNNEL_COLOURS)):
        centre = left + index * FUNNEL_PITCH + FUNNEL_STAGE_W / 2.0
        half = FUNNEL_STAGE_W / 2.0
        points = [
            (-half, -high / 2.0),
            (half, -low / 2.0),
            (half, low / 2.0),
            (-half, high / 2.0),
        ]
        layers.append(shape_layer(
            "funnel_stage_%d" % index,
            {
                "type": "path",
                "path": polyline_commands(points, close=True),
                "fill": linear_gradient(
                    (0.0, 0.0), (0.0, 1.0),
                    [(0.0, top, 1.0), (1.0, bottom, 1.0)],
                ),
            },
            (FUNNEL_STAGE_W + 8.0, high + 8.0),
            at(centre - W / 2.0, FUNNEL_CY),
            animation=fade_in(start=index * 5, length=10),
        ))
    return layers


def _step_layers() -> list:
    layers = [shape_layer(
        "step_chart",
        {
            "type": "path",
            "path": step_commands(STEP_XS, STEP_YS),
            "stroke": {"color": "#38e0d0", "width": 4},
        },
        (2 * max(abs(x) for x in STEP_XS) + 20.0, 180.0),
        at(STEP_CX, STEP_CY),
        animation=fade_in(start=6, length=12),
    )]
    for index, (x, y) in enumerate(zip(STEP_XS, STEP_YS)):
        layers.append(shape_layer(
            "step_marker_%d" % index,
            {"type": "ellipse", "fill": rgba("#ffd166")},
            (MARKER, MARKER), at(STEP_CX + x, STEP_CY + y),
            animation=fade_in(start=8 + index * 2, length=8),
        ))
    return layers


def _counter_layers() -> list:
    rows = (
        ("counter_leads", "Leads ${leads}", "#ffb35c"),
        ("counter_mql", "MQL ${mql}", "#9d8cff"),
        ("counter_sql", "SQL ${sql}", "#38bdf8"),
    )
    return [
        text_layer(layer_id, words, (240, 34), at(COUNTER_CX, y), 22.0, fill,
                   animation=fade_in(start=4 + index * 4, length=10))
        for index, ((layer_id, words, fill), y) in enumerate(zip(rows, COUNTER_YS))
    ]


def _badge_layers() -> list:
    return [
        shape_layer(
            "badge_%d" % index,
            {
                "type": "rounded_rect",
                "corner_radius": [12, 12, 12, 12],
                "fill": rgba("#1b2233"),
                "stroke": {"color": rgba("#38bdf8", 0.55), "width": 2},
            },
            (170, 44), at(-300.0 + index * 300.0, BADGE_Y),
            grid_layout={"group_id": "badges", "columns": 3, "gap": [24, 0]},
            animation=fade_in(start=10 + index * 3, length=10),
        )
        for index in range(3)
    ]


def build() -> dict:
    layers = []

    layers.append(shape_layer(
        "bg_gradient",
        {
            "type": "rect",
            "fill": linear_gradient(
                (0.0, 0.0), (1.0, 0.35),
                [(0.0, "#060a16", 1.0), (0.6, "#101a33", 1.0), (1.0, "#2a0812", 1.0)],
            ),
        },
        (W, H), at(0, 0),
        background_look={"type": "film_grade", "grain": 0.04, "seed": 11},
    ))

    layers.append(shape_layer(
        "dot_field",
        {"type": "dot_grid", "fill": rgba("#1b2440"), "spacing": 30, "dot_radius": 2.5},
        (W - 80.0, H - 80.0), at(0, 0),
    ))

    layers.append(text_layer("title", "2.5D PIPELINE", (560, 58), at(-250, -205),
                             40.0, "#F4F7FF"))
    layers.append(text_layer("subtitle", "motion graphics from plan primitives",
                             (700, 32), at(-140, -168), 19.0, "#8fa0c4"))

    # A 3D layer authors in the camera basis, not in canvas pixels: the canvas
    # centre is the origin and +y points *up*, so `at(300, 190)` is what lands
    # at canvas (780, 80).  See the convention table in README.md — this is
    # pinned by tests/render_graph/pipeline/test_pipeline_robustness_coordinate.cpp
    # ("Camera2_5D uses screen-Y-down coordinates: authored y=50 maps to
    # canvas y=540-50=490").
    layers.append(shape_layer(
        "tilted_ring",
        {
            "type": "ellipse",
            "fill": linear_gradient(
                (0.0, 0.0), (0.0, 1.0),
                [(0.0, "#ff6a4d", 1.0), (1.0, "#2f4bd6", 1.0)],
            ),
        },
        (300, 300), at(300, 190),
        enable_3d=True, rotation=[58, 0, 0], scale=[1.0, 0.35, 1.0],
    ))

    layers += _funnel_layers()
    layers += _counter_layers()
    layers += _step_layers()

    commands = bezier_commands(P0, P1, P2, P3)
    length = cubic_length(P0, P1, P2, P3)
    layers.append(shape_layer(
        "flow_ribbon",
        {
            "type": "path",
            "path": commands,
            "stroke": {
                "color": "#FFFFFF",
                "width": 7,
                "dash_array": [length, length],
                "dash_offset": length,
            },
        },
        (W, H), at(0, RIBBON_CENTRE_Y),
        animation={"tracks": [
            track("stroke_dash_offset",
                  [keyframe(6, length), keyframe(40, 0.0)], easing="in_out_cubic"),
        ]},
    ))
    layers.append(shape_layer(
        "ribbon_head",
        {"type": "ellipse", "fill": rgba("#FFFFFF")},
        (20, 20), at(0, RIBBON_CENTRE_Y),
        follow_path={
            "path_commands": commands,
            "progress": {"keyframes": [
                {"frame": 6, "value": 0.0},
                {"frame": 40, "value": 1.0},
            ]},
        },
    ))

    layers += _badge_layers()

    return plan(
        "infographic_2p5d_20_scene",
        layers,
        camera=one_to_one_camera(),
        variables={"leads": 12480, "mql": 3214, "sql": 842},
    )
