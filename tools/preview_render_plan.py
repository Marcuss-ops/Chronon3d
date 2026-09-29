#!/usr/bin/env python3
"""Faithfully preview a `chronon.render-plan.v2` file as frames, a GIF and an MP4.

This is not the production renderer. It is the fast path for *seeing* what a
plan encodes when the renderer's plan-animation path is unavailable on a host:
it composites exactly the baked tracks the plan carries — each image/text layer
at its interpolated position, scale and opacity, in plan order — over the same
assets the renderer would mount.

Because the plan is the contract, a preview that disagrees with the plan is a
bug in this file and not in the plan.

The composite is built at `--supersample` times the canvas (default 2) and
reduced once at the end. Every layer is therefore placed with sub-pixel accuracy,
which is what keeps a slow pan or push-in from stepping a whole pixel at a time —
the jitter that otherwise reads as a camera that will not sit still.

Usage
-----
  Chronon3d/tools/preview_render_plan.py --plan P.plan.json --assets-root Chronon3d \
      --out /tmp/preview.mp4 [--gif /tmp/preview.gif] [--stride 1] [--scale 1.0] \
      [--supersample 2]

Needs Pillow; MP4 muxing shells out to ffmpeg.
"""

import argparse
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

# A marker dot and a drop pin are symbols the demo plans carry as text. The plan's
# font may not cover them, so they are drawn as the shapes they name rather than
# as a missing-glyph box.
_MARKER_DOT = "\u25cf"
_DROP_PIN = "\U0001F4CD"


def _hex_to_rgb(value: str) -> tuple[int, int, int]:
    value = (value or "#FFFFFF").lstrip("#")
    if len(value) == 3:
        value = "".join(ch * 2 for ch in value)
    return tuple(int(value[i:i + 2], 16) for i in (0, 2, 4))


def _tracks(layer: dict) -> dict[str, dict[int, list[float]]]:
    """property -> {frame: values} for one layer's baked tracks."""

    out: dict[str, dict[int, list[float]]] = {}
    animation = layer.get("animation")
    if not animation:
        return out
    for track in animation.get("tracks", []):
        keys = {}
        for key in track.get("keyframes", []):
            keys[int(key["frame"])] = key["value"]
        out[track["property"]] = keys
    return out


def _at(keys: dict[int, list[float]], frame: int, count: int, fallback: list[float]) -> list[float]:
    """The track's value at `frame`: dense keys are indexed, sparse ones held."""

    if not keys:
        return fallback
    if frame in keys:
        return keys[frame]
    lo = max((f for f in keys if f <= frame), default=None)
    if lo is not None:
        return keys[lo]
    return keys[min(keys)]


class Previewer:
    def __init__(self, plan: dict, assets_root: Path, scale: float, supersample: int = 2):
        self.plan = plan
        self.root = assets_root
        self.canvas_w = int(plan["canvas"]["width"])
        self.canvas_h = int(plan["canvas"]["height"])
        self.fps_num = int(plan["canvas"].get("fps_num", 30))
        self.fps_den = int(plan["canvas"].get("fps_den", 1))
        self.frames = int(plan["canvas"]["duration_frames"])
        self.scale = scale
        # The composite is built at `supersample` times the canvas, then reduced
        # once at the end. Every layer is placed with fractional pixel accuracy in
        # that space, which is what keeps a slow pan or push-in from stepping a
        # whole pixel at a time — the jitter that reads as a shaking camera.
        self.ss = max(1, int(supersample))
        self._images: dict[str, Image.Image] = {}
        self._fonts: dict[tuple[str, int], ImageFont.FreeTypeFont] = {}
        # Bounded: a fixed-scale pan asks for one size forever, while a push-in
        # asks for a new one every frame, and an unbounded cache would hold the
        # whole clip's worth of full-resolution copies.
        self._tiles: dict[tuple[str, int, int], Image.Image] = {}
        self._tile_order: list[tuple[str, int, int]] = []
        self._tile_budget = 6

    def _image(self, asset: str) -> Image.Image | None:
        if asset not in self._images:
            path = self.root / asset
            self._images[asset] = Image.open(path).convert("RGBA") if path.exists() else None
        return self._images[asset]

    def _tile(self, asset: str, width: int, height: int) -> Image.Image | None:
        """The asset scaled to `width`x`height`, memoised across frames.

        A pan at a fixed scale asks for the same size on every frame, so the
        expensive resample happens once instead of once per frame.
        """

        key = (asset, width, height)
        cached = self._tiles.get(key)
        if cached is None:
            source = self._image(asset)
            if source is None:
                return None
            cached = source if (width, height) == source.size \
                else source.resize((width, height), Image.LANCZOS)
            self._tiles[key] = cached
            self._tile_order.append(key)
            while len(self._tile_order) > self._tile_budget:
                self._tiles.pop(self._tile_order.pop(0), None)
        return cached

    def _font(self, asset: str, size: int) -> ImageFont.FreeTypeFont:
        key = (asset, size)
        if key not in self._fonts:
            self._fonts[key] = ImageFont.truetype(str(self.root / asset), size)
        return self._fonts[key]

    def frame(self, index: int) -> Image.Image:
        ss = self.ss
        canvas = Image.new("RGBA", (self.canvas_w * ss, self.canvas_h * ss), (0, 0, 0, 255))
        for layer in self.plan.get("layers", []):
            tracks = _tracks(layer)
            opacity_keys = tracks.get("opacity", {})
            opacity = _at(opacity_keys, index, self.frames, [1.0])[0] if opacity_keys else 1.0
            if opacity <= 0.001:
                continue
            scale = _at(tracks.get("scale", {}), index, self.frames, [1.0, 1.0])[:2]
            pos = _at(tracks.get("position", {}), index, self.frames, [0.0, 0.0, 0.0])
            if layer.get("type") == "image":
                self._draw_image(canvas, layer, pos, scale, opacity)
            else:
                self._draw_text(canvas, layer, pos, scale, opacity)
        size = (int(self.canvas_w * self.scale), int(self.canvas_h * self.scale))
        return canvas.resize(size, Image.LANCZOS)

    def _draw_image(self, canvas, layer, pos, scale, opacity):
        ss = self.ss
        # An image layer's plan position is its centre minus the canvas centre;
        # the text half adds the centre back at decode. Recover the pixel here.
        cx = (pos[0] + self.canvas_w / 2.0) * ss
        cy = (pos[1] + self.canvas_h / 2.0) * ss
        box = layer.get("size")
        if not box:
            source = self._image(layer.get("asset", ""))
            if source is None:
                return
            box = [source.width, source.height]
        w = max(1, int(round(box[0] * scale[0] * ss)))
        h = max(1, int(round(box[1] * scale[1] * ss)))
        tile = self._tile(layer.get("asset", ""), w, h)
        if tile is None:
            return
        if opacity < 1.0:
            tile = tile.copy()
            tile.putalpha(tile.getchannel("A").point(lambda a: int(a * opacity)))
        canvas.paste(tile, (int(round(cx - w / 2.0)), int(round(cy - h / 2.0))), tile)

    def _draw_text(self, canvas, layer, pos, scale, opacity):
        ss = self.ss
        style = layer.get("style") or {}
        font_asset = style.get("font", "assets/fonts/Inter-Bold.ttf")
        size = max(1, int(round(float(style.get("font_size", 40)) * scale[1] * ss)))
        rgb = _hex_to_rgb(style.get("fill", "#FFFFFF"))
        colour = rgb + (int(255 * opacity),)
        text = layer.get("text", "")
        cx, cy = pos[0] * ss, pos[1] * ss

        if text in (_MARKER_DOT, _DROP_PIN):
            self._draw_symbol(canvas, text, cx, cy, size, colour)
            return

        font = self._font(font_asset, size)
        tile_w = max(8, int(font.getlength(text)) + 8)
        tile_h = int(size * 1.8)
        tile = Image.new("RGBA", (tile_w, tile_h), (0, 0, 0, 0))
        ImageDraw.Draw(tile).text((tile_w / 2.0, tile_h / 2.0), text, font=font, fill=colour,
                                  anchor="mm")
        canvas.paste(tile, (int(round(cx - tile_w / 2.0)), int(round(cy - tile_h / 2.0))), tile)

    def _draw_symbol(self, canvas, text, cx, cy, size, colour):
        box = max(8, size)
        tile = Image.new("RGBA", (box * 2, box * 2), (0, 0, 0, 0))
        draw = ImageDraw.Draw(tile)
        c = box
        if text == _MARKER_DOT:
            r = size * 0.34
            draw.ellipse([c - r, c - r, c + r, c + r], fill=colour)
        else:  # a drop pin: a disc over a point
            r = size * 0.30
            draw.ellipse([c - r, c - r * 1.5, c + r, c + r * 0.5], fill=colour)
            draw.polygon([(c - r * 0.75, c + r * 0.2), (c + r * 0.75, c + r * 0.2),
                          (c, c + r * 1.5)], fill=colour)
        canvas.paste(tile, (int(round(cx - c)), int(round(cy - c))), tile)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    parser.add_argument("--plan", required=True)
    parser.add_argument("--assets-root", required=True)
    parser.add_argument("--out", help="MP4 path")
    parser.add_argument("--gif", help="animated GIF path")
    parser.add_argument("--stride", type=int, default=1, help="render every Nth frame")
    parser.add_argument("--scale", type=float, default=1.0, help="output scale")
    parser.add_argument("--max-frames", type=int, default=0, help="cap the frame count")
    parser.add_argument("--supersample", type=int, default=2,
                        help="composite at N times the canvas, then reduce (1 disables)")
    args = parser.parse_args(argv)

    plan = json.loads(Path(args.plan).read_text())
    previewer = Previewer(plan, Path(args.assets_root).resolve(), args.scale, args.supersample)
    total = previewer.frames
    indices = list(range(0, total, max(1, args.stride)))
    if args.max_frames:
        indices = indices[:args.max_frames]

    tmp = Path(tempfile.mkdtemp(prefix="plan_preview_"))
    fps = previewer.fps_num / max(1, previewer.fps_den)
    try:
        gif_frames = []
        for i, frame_index in enumerate(indices):
            image = previewer.frame(frame_index)
            image.save(tmp / f"f{i:05d}.png")
            if args.gif:
                gif_frames.append(image.convert("P", palette=Image.ADAPTIVE))

        if args.out:
            subprocess.run(
                ["ffmpeg", "-y", "-framerate", f"{fps}", "-i", str(tmp / "f%05d.png"),
                 "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "18", args.out],
                check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            print(f"wrote {args.out} ({len(indices)} frames)")
        if args.gif and gif_frames:
            gif_frames[0].save(args.gif, save_all=True, append_images=gif_frames[1:],
                               duration=int(1000 / (fps / max(1, args.stride))), loop=0)
            print(f"wrote {args.gif} ({len(gif_frames)} frames)")
    finally:
        # The supersampled frames are large, and a run that dies part-way used to
        # leave half a gigabyte behind. The temp is ours; it always goes.
        shutil.rmtree(tmp, ignore_errors=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
