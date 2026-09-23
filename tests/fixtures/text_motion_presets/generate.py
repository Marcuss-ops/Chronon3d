#!/usr/bin/env python3
"""Build AE-style text motion examples from the canonical render-plan primitives."""
import json
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter

HERE = Path(__file__).resolve().parent
PLANS = HERE / "plans"
PLANS.mkdir(parents=True, exist_ok=True)
W, H, N = 960, 540, 90
FONT = "assets/fonts/Inter-Bold.ttf"
STAGE = HERE / "motion_text_stage.png"


def make_stage():
    """Render a restrained blue studio floor and horizon used by every sample."""
    image = Image.new("RGB", (W, H), (3, 5, 10))
    glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    draw = ImageDraw.Draw(glow)
    draw.ellipse((-300, 405, 1260, 720), fill=(18, 82, 148, 45))
    glow = glow.filter(ImageFilter.GaussianBlur(72))
    image = Image.alpha_composite(image.convert("RGBA"), glow)
    draw = ImageDraw.Draw(image)
    draw.line((0, 434, W, 434), fill=(61, 119, 170, 30), width=1)
    draw.ellipse((814, 431, 819, 436), fill=(219, 238, 255, 95))
    image.convert("RGB").save(STAGE, quality=95)


def keyframes(pairs):
    return [{"frame": f, "value": v} for f, v in pairs]


def track(prop, pairs, easing="in_out_cubic"):
    return {"property": prop, "easing": easing, "keyframes": keyframes(pairs)}


def selector(unit="grapheme", order="forward", start=None, end=None, shape="square", seed=None):
    row = {"unit": unit, "shape": shape, "order": order, "combine": "replace", "exclude_spaces": True}
    if start is not None:
        row["start"] = {"keyframes": keyframes(start), "easing": "linear"}
    if end is not None:
        row["end"] = {"keyframes": keyframes(end), "easing": "linear"}
    if seed is not None:
        row["randomize_order"] = True
        row["random_seed"] = seed
    return row


def animator(name, selectors, properties):
    return {"id": name, "selectors": selectors, "properties": properties}


def text_layer(text, *, id="title", animators=None, animation=None, position=(480, 270), size=(860, 280), font_size=128, fill="#F4F5F7", start=0, duration=N, glow=True):
    row = {"id": id, "type": "text", "text": text, "size": list(size), "position": list(position),
            "style": {"font": FONT, "font_size": font_size, "fill": fill},
            "start_frame": start, "duration_frames": duration, "text_animators": animators or []}
    if glow:
        row["style"]["glow"] = {"radius": 12.0, "intensity": 0.72, "color": "#91CFFF"}
    if animation:
        row["animation"] = {"tracks": animation}
    return row


def save(name, layers):
    scene = [{"id": "bg", "type": "image", "asset": "tests/fixtures/text_motion_presets/motion_text_stage.png",
              "fit": "stretch", "size": [W, H], "position": [0, 0],
              "start_frame": 0, "duration_frames": N}, *layers]
    plan = {"schema": "chronon.render-plan.v2", "version": 2, "job_id": "text_motion_" + name,
            "canvas": {"width": W, "height": H, "fps_num": 30, "fps_den": 1, "duration_frames": N},
            "layers": scene, "output": {"path": name + ".mp4", "format": "mp4", "codec": "h264"}}
    (PLANS / (name + ".plan.json")).write_text(json.dumps(plan, indent=2) + "\n")


def hidden_remainder(unit, order="forward", seed=None):
    # A moving selector applies opacity zero to units that have not entered yet.
    hide = animator("hide_unrevealed", [selector(unit, order, [(0, 0), (14, 100), (N - 1, 100)], [(0, 100), (N - 1, 100)], seed=seed)],
                    [track("opacity", [(0, 0), (N - 1, 0)], "linear")])
    # Soft frontier follows the reveal edge; it fades away once the reveal is complete.
    frontier = animator("soft_frontier", [selector(unit, order, [(0, 0), (11, 82), (N - 1, 100)],
                                                       [(0, 16), (11, 100), (N - 1, 100)], shape="smooth", seed=seed)],
                        [track("blur", [(0, 18), (7, 12), (14, 0), (N - 1, 0)], "out_cubic")])
    return [hide, frontier]


save("01_focus_typewriter", [text_layer("Motion Bear", animators=hidden_remainder("grapheme"))])
save("02_word_focus_forward", [text_layer("Animate your text effortlessly",
     animators=hidden_remainder("word"), font_size=64)])
save("03_word_focus_reverse", [text_layer("Supports any language.",
     animators=hidden_remainder("word", "reverse"), font_size=76)])
save("04_random_glyph_focus", [text_layer("4K Resolution",
     animators=hidden_remainder("glyph", "random", 1947), font_size=92)])

# Second line unfolds around its own center while the first stays still.
# Separate text layer makes the target explicit and keeps the preset portable
# until line-range selector percentages have a dedicated serialized contract.
unfold = animator("line_unfold", [selector("line")],
                  [track("opacity", [(0, 0), (12, 1), (71, 1)], "out_cubic"),
                   track("blur", [(0, 8), (18, 0), (71, 0)], "out_cubic")])
save("05_vertical_unfold", [text_layer("Text", id="first_line", position=(480, 205), font_size=112),
                            text_layer("Animations", id="unfolding_line", position=(480, 335),
                                       animators=[unfold],
                                       animation=[track("scale_y", [(0, 0.01), (3, 0.08), (12, 0.55),
                                                                     (20, 1.06), (25, 1.0), (N - 1, 1.0)], "out_cubic")],
                                       font_size=112)])

# Focus zoom composes blur, opacity and uniform scale on the same full-line selector.
focus_zoom = animator("focus_zoom", [selector("line")],
                      [track("opacity", [(0, 0), (10, 1), (54, 1), (66, 0)], "out_cubic"),
                       track("blur", [(0, 15), (10, 0), (54, 0), (66, 12)], "out_cubic"),
                       track("scale", [(0, 0.82), (13, 1), (54, 1), (66, 0.94)], "out_cubic")])
save("06_focus_zoom", [text_layer("Included", animators=[focus_zoom], font_size=136)])

# Three title cards follow the reference beats with optical overlap.
outgoing = animator("blur_fade_out", [selector("line")],
                    [track("opacity", [(0, 1), (12, 1), (20, 0)], "in_out_cubic"),
                     track("blur", [(0, 0), (12, 0), (20, 14)], "in_out_cubic")])
incoming = animator("blur_fade_in", [selector("line")],
                    [track("opacity", [(0, 0), (14, 0), (22, 1), (48, 1)], "in_out_cubic"),
                     track("blur", [(0, 14), (14, 14), (22, 0), (48, 0)], "in_out_cubic")])
middle_out = animator("middle_blur_out", [selector("line")],
                      [track("opacity", [(0, 0), (20, 1), (40, 1), (48, 0)], "in_out_cubic"),
                       track("blur", [(0, 14), (20, 0), (40, 0), (48, 14)], "in_out_cubic")])
last_in = animator("workflow_grow_in", [selector("line")],
                   [track("opacity", [(0, 0), (48, 0), (56, 1), (N - 1, 1)], "in_out_cubic"),
                    track("blur", [(0, 14), (48, 14), (56, 0), (N - 1, 0)], "in_out_cubic"),
                    track("scale", [(0, 0.92), (54, 1.08), (64, 1.0), (N - 1, 1.0)], "out_cubic")])
save("07_blur_crossfade", [
    text_layer("Save time", id="save_time", animators=[outgoing], position=(480, 250), font_size=100, start=0, duration=90),
    text_layer("boost your", id="boost_your", animators=[middle_out], position=(480, 250), font_size=100, fill="#D2B1FF", start=0, duration=90),
    text_layer("workflow", id="workflow", animators=[last_in], position=(480, 250), font_size=112, start=0, duration=90),
])

# Forward entrance, hold, then reverse-order erase from the right edge.
enter = animator("typewriter_enter", [selector("grapheme", "forward", [(0, 0), (13, 100), (N - 1, 100)],
                                                [(0, 100), (N - 1, 100)])],
                 [track("opacity", [(0, 0), (N - 1, 0)], "linear"),
                  track("blur", [(0, 14), (8, 6), (14, 0), (N - 1, 0)], "out_cubic")])
erase = animator("typewriter_erase_right_to_left", [selector("grapheme", "forward", [(0, 100), (62, 100), (N - 1, 0)],
                                                               [(0, 100), (N - 1, 100)])],
                 [track("opacity", [(0, 0), (N - 1, 0)], "linear")])
save("08_reverse_typewriter_erase", [text_layer("Thank you!", animators=[enter, erase], font_size=136)])

make_stage()
print(f"Generated {len(list(PLANS.glob('*.plan.json')))} motion preset plans in {PLANS}")
