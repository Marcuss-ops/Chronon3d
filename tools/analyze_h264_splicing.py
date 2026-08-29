#!/usr/bin/env python3
"""Measure whether an H.264 source is a candidate for safe packet/ROI patching.

This is deliberately a feasibility gate, not an encoder.  H.264 inter
prediction means that changing pixels in one picture generally requires
re-encoding dependent pictures; packet copying is safe only for untouched
closed GOPs.
"""

from __future__ import annotations

import argparse
import json
import subprocess
from collections import Counter
from pathlib import Path


def probe(path: str) -> dict:
    cmd = [
        "ffprobe", "-v", "error", "-of", "json",
        "-show_streams", "-show_frames", "-select_streams", "v:0", path,
    ]
    return json.loads(subprocess.check_output(cmd, text=True))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source")
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()

    data = probe(args.source)
    stream = data["streams"][0]
    frames = [f for f in data.get("frames", []) if f.get("media_type") == "video"]
    types = Counter(f.get("pict_type", "?") for f in frames)
    key_indices = [i for i, f in enumerate(frames) if f.get("key_frame") == 1]
    gop_sizes = [b - a for a, b in zip(key_indices, key_indices[1:])]
    if key_indices:
        gop_sizes.append(len(frames) - key_indices[-1])

    refs = int(stream.get("refs") or 0)
    has_b = int(stream.get("has_b_frames") or 0) > 0 or types.get("B", 0) > 0
    intra_only = bool(frames) and all(t == "I" for t in (f.get("pict_type") for f in frames))
    result = {
        "source": str(Path(args.source).resolve()),
        "codec": stream.get("codec_name"),
        "profile": stream.get("profile"),
        "width": stream.get("width"),
        "height": stream.get("height"),
        "pix_fmt": stream.get("pix_fmt"),
        "refs": refs,
        "has_b_frames": has_b,
        "frames": len(frames),
        "picture_types": dict(types),
        "keyframes": len(key_indices),
        "gop_sizes": gop_sizes,
        "gop_size_avg": (sum(gop_sizes) / len(gop_sizes)) if gop_sizes else None,
        "gop_size_max": max(gop_sizes) if gop_sizes else None,
        "packet_copy_only_safe_for_untouched_gops": True,
        "roi_patch_safe_without_dependent_reencode": intra_only,
        "verdict": (
            "INTRA_ONLY_ROI_PATCH_CANDIDATE" if intra_only else
            "NO_DIRECT_MACROBLOCK_PATCH_FOR_PIXEL_EDIT"
        ),
        "reason": (
            "Every picture is intra-coded." if intra_only else
            "Inter-coded pictures and/or reference frames require dependent "
            "picture reconstruction; an ROI NAL replacement is not generally "
            "equivalent to a pixel edit."
        ),
    }
    encoded = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.json:
        args.json.write_text(encoded)
    print(encoded, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
