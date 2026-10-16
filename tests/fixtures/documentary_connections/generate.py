"""Documentary connection demos v2.0: an entity line and a family tree, as render plans.

Writes two v3 render plans into `plans/` at 1920x1080, 30 fps, 6 seconds.
v2.0 adds a warm glow on every connector and node, hairline card borders and a
film-grade background. Render with:

    CLI=build/chronon/linux-video-fast-dev/apps/chronon3d_cli/chronon3d_cli
    $CLI render --plan plans/entity_line_h4_v2.plan.json \
        -o out/entity_line_h4_v2.mp4 --assets-root "$PWD" --backend software

Nodes are placeholder cards (initials + name); no photos are bundled.
"""

from __future__ import annotations

import json
import os

W, H, FPS, FRAMES = 1920, 1080, 30, 180
CX, CY = W / 2, H / 2
FONT = "assets/fonts/Inter-Bold.ttf"
SCHEMA = "chronon.render-plan.v3"
HERE = os.path.dirname(os.path.abspath(__file__))

INK = "#ece3cf"          # warm ivory for lines and primary text
GOLD = "#e3b86b"         # glow colour for connectors and nodes
CARD = "#1a1714"         # card body
CARD_EDGE = "#4a4034"    # hairline card border
PORTRAIT = "#3b3229"
MUTED = "#a89c8a"
PANEL_TOP = "#15110d"
PANEL_BOTTOM = "#070605"


def rgba(hex_color: str, alpha: float = 1.0) -> list:
    h = hex_color.lstrip("#")
    return [int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)] + [alpha]


def fill_vertical(top: str, bottom: str) -> dict:
    return {
        "type": "linear",
        "start": [0.0, 0.0],
        "end": [0.0, 1.0],
        "color_stops": [
            {"position": 0.0, "color": rgba(top)},
            {"position": 1.0, "color": rgba(bottom)},
        ],
    }


def glow(radius: float, intensity: float, color: str = GOLD) -> dict:
    return {"type": "glow", "radius": radius, "intensity": intensity, "color": rgba(color)}


def key(frame: int, value) -> dict:
    return {"frame": frame, "value": value}


def track(prop: str, keys: list, easing: str = "out_cubic") -> dict:
    return {"property": prop, "keyframes": keys, "easing": easing}


def reveal(start: int, length: int = 16, scale_from: float = 0.95) -> dict:
    """Card / node entrance: fade in and settle from a slight scale."""
    return {"tracks": [
        track("opacity", [key(start, 0.0), key(start + length, 1.0)]),
        track("scale", [key(start, [scale_from, scale_from, 1.0]),
                        key(start + length, [1.0, 1.0, 1.0])]),
    ]}


def grow(prop: str, start: int, length: int) -> dict:
    """Line growth from its own centre along one axis, with a soft opacity ramp."""
    return {"tracks": [
        track(prop, [key(start, 0.0), key(start + length, 1.0)], easing="in_out_cubic"),
        track("opacity", [key(start, 0.0), key(start + 4, 1.0)]),
    ]}


def rect(layer_id, size, pos, fill, start, anim=None, radius=None, effects=None):
    shape = {"type": "rounded_rect" if radius else "rect", "fill": fill}
    if radius:
        shape["corner_radius"] = [radius] * 4
    layer = {
        "id": layer_id, "type": "shape", "size": list(size), "position": list(pos),
        "start_frame": start, "duration_frames": FRAMES - start,
        "shape": shape,
    }
    if anim:
        layer["animation"] = anim
    if effects:
        layer["effects"] = effects
    return layer


def text(layer_id, words, size, pos, font_size, fill, start, anim=None):
    layer = {
        "id": layer_id, "type": "text", "size": list(size), "position": list(pos),
        "start_frame": start, "duration_frames": FRAMES - start,
        "text": words,
        "style": {"font": FONT, "font_size": font_size, "fill": fill},
    }
    if anim:
        layer["animation"] = anim
    return layer


def background() -> dict:
    return rect("bg", (W, H), (CX, CY), rgba(PANEL_TOP), 0)


def node_dot(prefix, x, y, start):
    return rect(f"{prefix}_dot", (14, 14), (x, y), rgba(INK), start,
                anim=reveal(start, 8, 0.5), radius=7,
                effects=[glow(16, 1.3)])


def person_card(prefix, x, y, card_w, card_h, name, role, initials, start):
    """Card with a hairline border, placeholder portrait, initials, name and role."""
    portrait_h = int(card_h * 0.62)
    top = y - card_h / 2
    portrait_y = top + 14 + portrait_h / 2
    return [
        rect(f"{prefix}_border", (card_w + 2, card_h + 2), (x, y), rgba(CARD_EDGE), start,
             anim=reveal(start), radius=17),
        rect(f"{prefix}_card", (card_w, card_h), (x, y), rgba(CARD), start,
             anim=reveal(start), radius=16),
        rect(f"{prefix}_portrait", (card_w - 26, portrait_h), (x, portrait_y), rgba(PORTRAIT),
             start, anim=reveal(start), radius=10),
        text(f"{prefix}_initials", initials, (card_w, 80), (x, portrait_y), 56, INK,
             start + 2, anim=reveal(start + 2)),
        text(f"{prefix}_name", name, (card_w - 20, 44), (x, y + card_h / 2 - 58), 34, INK,
             start + 6, anim=reveal(start + 6)),
        text(f"{prefix}_role", role, (card_w - 20, 32), (x, y + card_h / 2 - 22), 24, MUTED,
             start + 10, anim=reveal(start + 10)),
    ]


def entity_line_h4() -> dict:
    """Horizontal line with four entity nodes: the line draws, then nodes arrive in turn."""
    people = [
        ("Persona A", "Fondatore", "PA", -690),
        ("Persona B", "Socio", "PB", -230),
        ("Persona C", "Testimone", "PC", 230),
        ("Persona D", "Erede", "PD", 690),
    ]
    line_y = CY
    layers = [background()]
    layers.append(rect("main_line", (1500, 2), (CX, line_y), rgba(INK), 0,
                       anim=grow("scale_x", 6, 24), effects=[glow(20, 1.2)]))
    for i, (name, role, initials, dx) in enumerate(people):
        start = 30 + i * 24
        x = CX + dx
        card_y = line_y - 230
        layers.append(rect(f"conn_{i}", (2, 70), (x, line_y - 110), rgba(INK), start,
                           anim=grow("scale_y", start, 10), effects=[glow(10, 0.8)]))
        layers.append(node_dot(f"node{i}", x, line_y, start))
        layers += person_card(f"p{i}", x, card_y, 300, 300, name, role, initials, start + 6)
    return plan("entity_line_h4_v2", layers)


def family_tree_1p4() -> dict:
    """One parent on top, a bus line, and four children hanging from it."""
    layers = [background()]
    parent_y = 250
    bus_y = 500
    child_y = 700
    kids = [("Figlio 1", "Figlio"), ("Figlio 2", "Figlia"),
            ("Figlio 3", "Figlio"), ("Figlio 4", "Figlia")]
    xs = [CX - 630, CX - 210, CX + 210, CX + 630]

    layers += person_card("parent", CX, parent_y, 300, 300, "Padre", "Capostipite", "PD", 0)
    layers.append(rect("spine", (2, 110), (CX, (parent_y + 150 + bus_y) / 2), rgba(INK), 24,
                       anim=grow("scale_y", 24, 12), effects=[glow(10, 0.8)]))
    layers.append(rect("bus", (1260, 2), (CX, bus_y), rgba(INK), 36,
                       anim=grow("scale_x", 36, 24), effects=[glow(20, 1.2)]))
    layers.append(node_dot("parent_node", CX, bus_y, 36))
    for i, ((name, role), x) in enumerate(zip(kids, xs)):
        start = 66 + i * 18
        drop_top = bus_y
        drop_bottom = child_y - 120
        layers.append(rect(f"drop_{i}", (2, drop_bottom - drop_top),
                           (x, (drop_top + drop_bottom) / 2), rgba(INK), start,
                           anim=grow("scale_y", start, 10), effects=[glow(10, 0.8)]))
        layers.append(node_dot(f"kid{i}_node", x, drop_top, start))
        layers += person_card(f"kid{i}", x, child_y, 260, 240, name, role,
                              f"F{i + 1}", start + 8)
    return plan("family_tree_1p4_v2", layers)


def plan(job_id: str, layers: list) -> dict:
    return {
        "schema": SCHEMA,
        "version": 3,
        "job_id": job_id,
        "canvas": {"width": W, "height": H, "fps_num": FPS, "fps_den": 1,
                   "duration_frames": FRAMES},
        "output": {"path": job_id + ".mp4", "format": "mp4", "codec": "h264"},
        "layers": layers,
    }


def main() -> None:
    out_dir = os.path.join(HERE, "plans")
    os.makedirs(out_dir, exist_ok=True)
    for doc in (entity_line_h4(), family_tree_1p4()):
        path = os.path.join(out_dir, doc["job_id"] + ".plan.json")
        with open(path, "w", encoding="utf-8") as fh:
            json.dump(doc, fh, indent=2)
            fh.write("\n")
        print(path)


if __name__ == "__main__":
    main()
