#!/usr/bin/env python3
"""Extract one frame from the pre-encode raw sink as a lossless PNG.

TICKET-TEXT-QUALITY-V2 P0-0b / TICKET-IMAGE-PATH-PIXEL-TRUTH.

Why this exists
---------------
The still/PNG image lane (`render -o x.png`) is NOT a pixel-truth surface: it
does not compose the text layers and does not vary per frame or per backend
(`docs/evidence/image-path-defect-2026-09-16.md`). The canonical pre-encode
truth is the raw video sink (`--video-sink raw`): host RGBA8 frames written with
no encoder, no container and no chroma conversion, so what a quality
measurement sees is the renderer, not a converter.

The raw sink writes ONE file holding `frames x frame_bytes` concatenated, tight
row-major RGBA8 frames (`src/media/video/raw_video_sink.cpp`,
`frame_bytes = width * height * 4`). Nothing in the repository turned such a
file into the frame a pixel-level diagnosis needs, so the quality campaign
could not name its own input. This tool is that missing step, and it is
deliberately arithmetic: it refuses a file whose size is not a whole multiple of
the frame size, instead of guessing a layout and reporting a wrong picture.

The PNG is written verbatim (RGBA8, no colour transform, filter type 0), so
`--out` is a byte-faithful projection of the renderer's frame. The profile it
prints are the same statistics the text-quality evidence uses (distinct levels,
ink fraction, luma percentiles), which is what makes "contains text" and
"uniform frame" distinguishable numbers rather than impressions.

Usage
-----
  # one frame from a raw sink artifact
  tools/raw_frame_to_png.py --raw frames.raw --width 1920 --height 1080 \
      --frame 60 --out flat_frame60.png

  # gate: fail when the frame is (almost) uniform, i.e. carries no composed
  # content — the exact condition `flat_still.png` had to stop meeting (P0-0b)
  tools/raw_frame_to_png.py --raw frames.raw --width 1920 --height 1080 \
      --frame 60 --out f60.png --require-content 0.05

  # prove the writer round-trips before trusting it
  tools/raw_frame_to_png.py --self-test
"""

from __future__ import annotations

import argparse
import struct
import sys
import zlib
from pathlib import Path

CHANNELS = 4  # RGBA8, the raw sink's host format on the pixel-truth lane
WHITE_LUMA = 0.72  # matches the text-quality evidence's white-pixel threshold


def frame_bytes(width: int, height: int) -> int:
    return width * height * CHANNELS


def read_frame(path: Path, width: int, height: int, index: int) -> tuple[bytes, int]:
    """Return (frame bytes, total frames) for one index of a raw sink file.

    Fails closed on a size that is not a whole number of frames: a truncated or
    wrong-shaped artifact must never be silently reinterpreted as a picture.
    """
    size = path.stat().st_size
    per_frame = frame_bytes(width, height)
    if per_frame == 0:
        raise ValueError("width*height must be non-zero")
    if size == 0:
        raise ValueError(f"{path} is empty")
    if size % per_frame != 0:
        raise ValueError(
            f"{path} is {size} bytes, not a multiple of {per_frame} "
            f"({width}x{height}x{CHANNELS}); the raw sink contract is "
            "frames x frame_bytes"
        )
    frames = size // per_frame
    if not 0 <= index < frames:
        raise ValueError(f"--frame {index} out of range: file holds {frames} frames")
    with path.open("rb") as handle:
        handle.seek(index * per_frame)
        data = handle.read(per_frame)
    if len(data) != per_frame:
        raise ValueError(f"short read for frame {index}: {len(data)} != {per_frame}")
    return data, frames


def png_chunk(tag: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload))
        + tag
        + payload
        + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)
    )


def encode_png_rgba8(width: int, height: int, pixels: bytes) -> bytes:
    """Encode tight RGBA8 pixels as a PNG with no colour transform.

    Every scanline uses filter type 0 (None), so the encoded stream is an exact
    copy of the input rows: the artifact cannot disagree with the renderer.
    """
    expected = width * height * CHANNELS
    if len(pixels) != expected:
        raise ValueError(f"pixel buffer is {len(pixels)} bytes, expected {expected}")
    stride = width * CHANNELS
    raw = bytearray()
    for row in range(height):
        raw.append(0)  # filter: None
        raw += pixels[row * stride : (row + 1) * stride]
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", ihdr)
        + png_chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + png_chunk(b"IEND", b"")
    )


def decode_png_rgba8_none_filter(png: bytes) -> tuple[int, int, bytes]:
    """Read back a PNG written by encode_png_rgba8 (filter None only).

    Used by --self-test as the round-trip witness. It intentionally supports
    only what this tool writes; anything else is a hard error rather than an
    approximate decode.
    """
    if not png.startswith(b"\x89PNG\r\n\x1a\n"):
        raise ValueError("not a PNG signature")
    offset = 8
    width = height = 0
    idat = bytearray()
    while offset < len(png):
        (length,) = struct.unpack(">I", png[offset : offset + 4])
        tag = png[offset + 4 : offset + 8]
        payload = png[offset + 8 : offset + 8 + length]
        offset += 12 + length
        if tag == b"IHDR":
            width, height, depth, color, comp, filt, interlace = struct.unpack(
                ">IIBBBBB", payload
            )
            if (depth, color, comp, filt, interlace) != (8, 6, 0, 0, 0):
                raise ValueError("unsupported PNG shape for this reader")
        elif tag == b"IDAT":
            idat += payload
        elif tag == b"IEND":
            break
    raw = zlib.decompress(bytes(idat))
    stride = width * CHANNELS
    out = bytearray()
    for row in range(height):
        base = row * (stride + 1)
        if raw[base] != 0:
            raise ValueError("unexpected PNG filter type")
        out += raw[base + 1 : base + 1 + stride]
    return width, height, bytes(out)


def profile_rgba8(pixels: bytes, width: int, height: int) -> dict:
    """Luma statistics that separate 'rendered text' from 'uniform frame'.

    `content_fraction` is the discriminating number: the fraction of pixels
    whose luma differs from the frame's modal (background) luma. A still-lane
    frame is uniform, so it scores 0 whatever its background colour is
    (the observed defect was a flat grey canvas, luma 0.498); a frame that
    carries composed text on any canvas scores above 0. Opaque-pixel counts do
    NOT discriminate here, which is why this tool does not offer them as a gate:
    a fully opaque flat canvas is exactly the thing that must fail.
    """
    total = width * height
    luma = bytearray(total)
    histogram = [0] * 256
    white = 0
    for i in range(total):
        r, g, b, _a = pixels[i * 4 : i * 4 + 4]
        # Rec.601 luma on the stored sRGB-encoded bytes: a stable, monotone
        # measure of "is there content", not a colour-managed conversion.
        y = int(round(0.299 * r + 0.587 * g + 0.114 * b))
        luma[i] = y
        histogram[y] += 1
        if y / 255.0 > WHITE_LUMA:
            white += 1
    ordered = sorted(luma)
    background = max(range(256), key=lambda level: histogram[level])

    def pct(p: float) -> float:
        return ordered[min(total - 1, int(p * total))] / 255.0

    return {
        "width": width,
        "height": height,
        "distinct_levels": len(set(luma)),
        "luma_min": round(ordered[0] / 255.0, 4),
        "luma_p50": round(pct(0.50), 4),
        "luma_p99": round(pct(0.99), 4),
        "luma_max": round(ordered[-1] / 255.0, 4),
        "background_luma": round(background / 255.0, 4),
        "white_pixels": white,
        "content_fraction": round((total - histogram[background]) / total, 6),
    }


def self_test() -> int:
    """Round-trip a synthetic frame through the writer and the reader."""
    width, height = 37, 11  # deliberately not power-of-two
    pixels = bytearray()
    for y in range(height):
        for x in range(width):
            pixels += bytes(((x * 7) % 256, (y * 13) % 256, (x + y) % 256,
                             255 if (x + y) % 3 else 0))
    png = encode_png_rgba8(width, height, bytes(pixels))
    rw, rh, back = decode_png_rgba8_none_filter(png)
    if (rw, rh) != (width, height) or back != bytes(pixels):
        print("self-test FAILED: PNG round-trip changed the pixels", file=sys.stderr)
        return 1
    prof = profile_rgba8(bytes(pixels), width, height)
    if prof["distinct_levels"] < 2 or prof["content_fraction"] <= 0:
        print("self-test FAILED: profile collapsed a varied frame", file=sys.stderr)
        return 1
    # A uniform frame must be recognisable as uniform by the same profile —
    # including a flat GREY canvas, which is the shape the still lane produced
    # (an opacity-0 canvas and a flat grey canvas are the same problem here).
    for level in (0, 32, 128, 255):
        flat = bytes([level, level, level, 255]) * (width * height)
        flat_prof = profile_rgba8(flat, width, height)
        if flat_prof["distinct_levels"] != 1 or flat_prof["content_fraction"] != 0.0:
            print(f"self-test FAILED: flat luma {level} was reported as content",
                  file=sys.stderr)
            return 1
    print("self-test OK: PNG round-trip is byte-faithful; profile separates a "
          "varied frame from a uniform one on any background")
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--raw", type=Path, help="raw sink artifact (concatenated RGBA8 frames)")
    parser.add_argument("-w", "--width", type=int, help="frame width in pixels")
    # No short flag for --height: `-h` belongs to argparse's help.
    parser.add_argument("--height", type=int, help="frame height in pixels")
    parser.add_argument("--frame", type=int, default=0, help="frame index (default 0)")
    parser.add_argument("--out", type=Path, help="PNG to write (verbatim frame bytes)")
    parser.add_argument("--require-content", type=float, default=None, dest="require_content",
                        help="exit non-zero when content_fraction (pixels differing "
                             "from the modal background luma) is below this value")
    parser.add_argument("--self-test", action="store_true",
                        help="verify the writer/reader round-trip and exit")
    args = parser.parse_args(argv)

    if args.self_test:
        return self_test()

    for name in ("raw", "width", "height", "out"):
        if getattr(args, name) is None:
            parser.error(f"--{name} is required (or use --self-test)")
    if args.width <= 0 or args.height <= 0:
        parser.error("--width/--height must be positive")

    try:
        pixels, frames = read_frame(args.raw, args.width, args.height, args.frame)
    except (OSError, ValueError) as exc:
        print(f"raw_frame_to_png: {exc}", file=sys.stderr)
        return 2

    png = encode_png_rgba8(args.width, args.height, pixels)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(png)

    prof = profile_rgba8(pixels, args.width, args.height)
    print(f"raw: {args.raw} frames={frames} frame_bytes={frame_bytes(args.width, args.height)}")
    print(f"png: {args.out} ({len(png)} bytes, verbatim RGBA8)")
    print("profile: " + " ".join(f"{k}={v}" for k, v in prof.items()))

    if args.require_content is not None and prof["content_fraction"] < args.require_content:
        print(
            f"raw_frame_to_png: content_fraction {prof['content_fraction']} < "
            f"required {args.require_content}: this frame is uniform and does "
            "not carry the composed content",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
