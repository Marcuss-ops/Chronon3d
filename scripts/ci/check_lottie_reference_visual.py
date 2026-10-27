#!/usr/bin/env python3
"""Compare Chronon software frames to pinned Lottie-web SVG reference renders."""
from __future__ import annotations

import hashlib
import json
import os
import struct
import subprocess
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "golden" / "lottie_cli_gate"
REFERENCE = FIXTURE / "reference"
MANIFEST = REFERENCE / "manifest.json"
PLAN = FIXTURE / "mark_anim_reference_plan.json"
WIDTH, HEIGHT = 320, 240
MIN_SSIM = 0.99


def read_png(path: Path) -> tuple[int, int, list[tuple[int, int, int]]]:
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path} is not a PNG")
    offset = 8
    compressed = bytearray()
    width = height = bit_depth = color_type = interlace = 0
    while offset < len(data):
        length = struct.unpack_from(">I", data, offset)[0]
        kind = data[offset + 4:offset + 8]
        payload = data[offset + 8:offset + 8 + length]
        offset += 12 + length
        if kind == b"IHDR":
            width, height, bit_depth, color_type, compression, filtering, interlace = struct.unpack(">IIBBBBB", payload)
            if compression or filtering or bit_depth != 8 or interlace:
                raise ValueError("reference PNG must use non-interlaced 8-bit channels")
        elif kind == b"IDAT":
            compressed.extend(payload)
        elif kind == b"IEND":
            break
    channels = {2: 3, 6: 4}.get(color_type)
    if channels is None or width <= 0 or height <= 0:
        raise ValueError("unsupported reference PNG color type or dimensions")
    stride = width * channels
    decoded = zlib.decompress(compressed)
    rows: list[bytearray] = []
    cursor = 0
    previous = bytearray(stride)
    for _ in range(height):
        filter_type = decoded[cursor]
        cursor += 1
        scan = bytearray(decoded[cursor:cursor + stride])
        cursor += stride
        for i in range(stride):
            left = scan[i - channels] if i >= channels else 0
            up = previous[i]
            upper_left = previous[i - channels] if i >= channels else 0
            if filter_type == 1:
                scan[i] = (scan[i] + left) & 255
            elif filter_type == 2:
                scan[i] = (scan[i] + up) & 255
            elif filter_type == 3:
                scan[i] = (scan[i] + ((left + up) // 2)) & 255
            elif filter_type == 4:
                p = left + up - upper_left
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - upper_left)
                predictor = left if pa <= pb and pa <= pc else up if pb <= pc else upper_left
                scan[i] = (scan[i] + predictor) & 255
            elif filter_type != 0:
                raise ValueError(f"unsupported PNG filter {filter_type}")
        rows.append(scan)
        previous = scan
    pixels: list[tuple[int, int, int]] = []
    for row in rows:
        for x in range(width):
            pos = x * channels
            pixels.append((row[pos], row[pos + 1], row[pos + 2]))
    return width, height, pixels


def chronon_rgb(raw: bytes, width: int = WIDTH, height: int = HEIGHT) -> list[tuple[int, int, int]]:
    if len(raw) != width * height * 4:
        raise ValueError(f"raw frame has {len(raw)} bytes; expected {width * height * 4}")
    pixels = []
    for i in range(0, len(raw), 4):
        alpha = raw[i + 3]
        pixels.append(tuple((raw[i + c] * alpha + 127) // 255 for c in range(3)))
    return pixels


def ssim(a: list[tuple[int, int, int]], b: list[tuple[int, int, int]],
         width: int, box: tuple[int, int, int, int]) -> float:
    left, top, right, bottom = box
    count = (right - left) * (bottom - top)
    scores = []
    c1, c2 = (0.01 * 255) ** 2, (0.03 * 255) ** 2
    for channel in range(3):
        av = [a[y * width + x][channel] for y in range(top, bottom) for x in range(left, right)]
        bv = [b[y * width + x][channel] for y in range(top, bottom) for x in range(left, right)]
        mean_a, mean_b = sum(av) / count, sum(bv) / count
        var_a = sum((v - mean_a) ** 2 for v in av) / max(1, count - 1)
        var_b = sum((v - mean_b) ** 2 for v in bv) / max(1, count - 1)
        cov = sum((x - mean_a) * (y - mean_b) for x, y in zip(av, bv)) / max(1, count - 1)
        scores.append(((2 * mean_a * mean_b + c1) * (2 * cov + c2)) /
                      ((mean_a * mean_a + mean_b * mean_b + c1) * (var_a + var_b + c2)))
    return sum(scores) / len(scores)


def cli_path() -> Path:
    override = os.environ.get("CHRONON_CLI")
    path = Path(override) if override else ROOT / "build/chronon/linux-video-fast-dev/apps/chronon3d_cli/chronon3d_cli"
    if not path.is_file():
        raise SystemExit(f"chronon3d_cli not found: {path}")
    return path


def main() -> int:
    manifest = json.loads(MANIFEST.read_text())
    input_path = FIXTURE / "mark_anim_reference.json"
    if hashlib.sha256(input_path.read_bytes()).hexdigest() != manifest["input_sha256"]:
        raise SystemExit("pinned Lottie-web source fixture hash mismatch")
    for name, expected in manifest["references"].items():
        if hashlib.sha256((REFERENCE / name).read_bytes()).hexdigest() != expected:
            raise SystemExit(f"pinned reference image hash mismatch: {name}")

    cli = cli_path()
    validate = subprocess.run(
        [str(cli), "validate", "--plan", str(PLAN), "--assets-root", str(FIXTURE)],
        capture_output=True, text=True)
    if validate.returncode:
        print("FAIL: reference fixture validation\n" + validate.stderr[-2000:])
        return 1

    scores = {}
    with tempfile.TemporaryDirectory(prefix="lottie_visual_") as temp:
        for frame in (0, 30):
            raw_path = Path(temp) / f"chronon_{frame}.rgba"
            render = subprocess.run(
                [str(cli), "render", "--plan", str(PLAN), "--assets-root", str(FIXTURE),
                 "--backend", "software", "--start-frame", str(frame), "--end-frame", str(frame),
                 "--video-sink", "raw", "--output", str(raw_path)],
                capture_output=True, text=True)
            if render.returncode or not raw_path.is_file():
                print(f"FAIL: Chronon raw frame {frame}\n" + render.stdout[-2000:] + render.stderr[-2000:])
                return 1
            reference_path = REFERENCE / f"lottie_web_5_13_0_frame_{frame}.png"
            ref_w, ref_h, ref_pixels = read_png(reference_path)
            if (ref_w, ref_h) != (WIDTH, HEIGHT):
                print(f"FAIL: reference frame {frame} has unexpected size {ref_w}x{ref_h}")
                return 1
            current = chronon_rgb(raw_path.read_bytes())
            full = ssim(ref_pixels, current, WIDTH, (0, 0, WIDTH, HEIGHT))
            roi = (0, 80, 60, 160) if frame == 0 else (260, 80, WIDTH, 160)
            significant = ssim(ref_pixels, current, WIDTH, roi)
            scores[str(frame)] = {"global_ssim": full, "subject_roi_ssim": significant}
            print(f"Lottie-web 5.13.0 frame {frame}: global SSIM={full:.6f}, "
                  f"subject ROI SSIM={significant:.6f}")
            if full < MIN_SSIM or significant < MIN_SSIM:
                print(f"FAIL: frame {frame} SSIM is below {MIN_SSIM}")
                return 1
    print("LOTTIE_REFERENCE_PARITY PASS " + json.dumps(scores, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
