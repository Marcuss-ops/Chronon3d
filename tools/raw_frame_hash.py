#!/usr/bin/env python3
"""Emit a deterministic per-frame SHA-256 manifest for the raw sink.

The raw sink is the canonical pre-encode pixel truth: tight row-major RGBA8
frames concatenated without a container. This tool hashes each complete frame,
so machine gates can compare render output without storing PNG goldens.

Format (CHRONON FRAMEHASH v1)::

    CHRONON FRAMEHASH v1
    engine_sha=<sha or unknown>
    plan_digest=<digest or unknown>
    pixel_format=rgba8
    width=<pixels>
    height=<pixels>
    frame_bytes=<bytes>
    frames=<count>
    frame=0 sha256=<hex>
    ...

Usage::

    tools/raw_frame_hash.py --raw frames.raw --width 1920 --height 1080 \
        --engine-sha c6f4c69d1 --plan-digest <digest> --out frames.framehash
"""

from __future__ import annotations

import argparse
import difflib
import hashlib
import sys
from pathlib import Path

CHANNELS = 4
VERSION = "CHRONON FRAMEHASH v1"


def frame_bytes(width: int, height: int) -> int:
    if width <= 0 or height <= 0:
        raise ValueError("width and height must be positive")
    return width * height * CHANNELS


def frame_hashes(path: Path, per_frame: int) -> list[str]:
    size = path.stat().st_size
    if size == 0:
        raise ValueError(f"{path} is empty")
    if size % per_frame != 0:
        raise ValueError(
            f"{path} is {size} bytes, not a multiple of {per_frame}; "
            "the raw sink contract is frames x frame_bytes"
        )
    hashes: list[str] = []
    with path.open("rb") as handle:
        while True:
            frame = handle.read(per_frame)
            if not frame:
                break
            if len(frame) != per_frame:
                raise ValueError("short frame read")
            hashes.append(hashlib.sha256(frame).hexdigest())
    return hashes


def manifest(
    *,
    engine_sha: str,
    plan_digest: str,
    width: int,
    height: int,
    hashes: list[str],
) -> str:
    per_frame = frame_bytes(width, height)
    lines = [
        VERSION,
        f"engine_sha={engine_sha}",
        f"plan_digest={plan_digest}",
        "pixel_format=rgba8",
        f"width={width}",
        f"height={height}",
        f"frame_bytes={per_frame}",
        f"frames={len(hashes)}",
    ]
    lines.extend(f"frame={i} sha256={digest}" for i, digest in enumerate(hashes))
    return "\n".join(lines) + "\n"


def self_test() -> int:
    import tempfile

    width, height = 2, 1
    frames = [bytes((255, 0, 0, 255, 0, 0, 0, 255)), bytes(8)]
    with tempfile.TemporaryDirectory() as directory:
        raw = Path(directory) / "frames.raw"
        raw.write_bytes(b"".join(frames))
        hashes = frame_hashes(raw, frame_bytes(width, height))
        text = manifest(
            engine_sha="test", plan_digest="test", width=width, height=height,
            hashes=hashes)
        if text.count("frame=") != 2 or hashes[0] == hashes[1]:
            print("raw_frame_hash: self-test failed", file=sys.stderr)
            return 1
    print("self-test OK: two distinct RGBA8 frames hashed independently")
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--raw", type=Path)
    parser.add_argument("--width", type=int)
    parser.add_argument("--height", type=int)
    parser.add_argument("--engine-sha", default="unknown")
    parser.add_argument("--plan-digest", default="unknown")
    parser.add_argument("--out", type=Path)
    parser.add_argument("--golden", type=Path,
                        help="compare the generated manifest with this golden")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)
    if args.self_test:
        return self_test()
    for name in ("raw", "width", "height"):
        if getattr(args, name) is None:
            parser.error(f"--{name} is required (or use --self-test)")
    if args.width <= 0 or args.height <= 0:
        parser.error("--width/--height must be positive")

    try:
        hashes = frame_hashes(args.raw, frame_bytes(args.width, args.height))
        text = manifest(
            engine_sha=args.engine_sha,
            plan_digest=args.plan_digest,
            width=args.width,
            height=args.height,
            hashes=hashes,
        )
    except (OSError, ValueError) as exc:
        print(f"raw_frame_hash: {exc}", file=sys.stderr)
        return 2

    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(text, encoding="utf-8")
    if args.golden:
        try:
            expected = args.golden.read_text(encoding="utf-8")
        except OSError as exc:
            print(f"raw_frame_hash: cannot read golden: {exc}", file=sys.stderr)
            return 2
        if text != expected:
            diff = difflib.unified_diff(
                expected.splitlines(), text.splitlines(),
                fromfile=str(args.golden), tofile="generated", lineterm="")
            print("\n".join(diff), file=sys.stderr)
            return 1
        print(f"FRAMEHASH_PASS {args.golden}")
    elif not args.out:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
