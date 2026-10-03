#!/usr/bin/env python3
"""Create an agent-callable semantic subject matte using local rembg/ONNX."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile

from PIL import Image, ImageOps


def atomic_json(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            json.dump(value, stream, indent=2, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("--output", type=Path, required=True,
                        help="grayscale PNG matte (published only if gates pass)")
    parser.add_argument("--report", type=Path,
                        help="JSON report (default: OUTPUT.json)")
    parser.add_argument("--model", default="u2netp",
                        help="local rembg model, e.g. u2netp, isnet-general-use, sam")
    parser.add_argument("--minimum-foreground-fraction", type=float, default=0.002)
    parser.add_argument("--maximum-foreground-fraction", type=float, default=0.98)
    parser.add_argument("--maximum-mean-uncertainty", type=float, default=1.0,
                        help="gate mean(4*a*(1-a)); 1 disables uncertainty rejection")
    parser.add_argument("--post-process", action="store_true",
                        help="apply rembg's mask post-processing before gating")
    args = parser.parse_args()
    report_path = args.report or Path(str(args.output) + ".json")

    if not (0.0 <= args.minimum_foreground_fraction < args.maximum_foreground_fraction <= 1.0):
        parser.error("foreground fractions must satisfy 0 <= min < max <= 1")
    if not (0.0 <= args.maximum_mean_uncertainty <= 1.0):
        parser.error("maximum mean uncertainty must be in [0,1]")
    if not args.input.is_file():
        parser.error(f"input is not a file: {args.input}")
    if args.input.resolve() == args.output.resolve():
        parser.error("matte output must not replace the input image")
    if report_path.resolve() == args.output.resolve():
        parser.error("JSON report and matte output must use different paths")
    if args.input.stat().st_size > 256 * 1024 * 1024:
        parser.error("input file exceeds the 256 MiB safety limit")

    try:
        from rembg import new_session, remove
        import numpy as np
    except ImportError as error:
        raise SystemExit("Install the optional dependency with: pip install -r tools/requirements-agent-visual-segmentation.txt") from error

    input_bytes = args.input.read_bytes()
    with Image.open(args.input) as opened:
        image = ImageOps.exif_transpose(opened).convert("RGBA")
    width, height = image.size
    if width <= 0 or height <= 0 or width * height > 8_388_608:
        parser.error("input dimensions must be positive and no larger than 8,388,608 pixels")

    # The provider runs locally through ONNX Runtime. Model acquisition is
    # managed by rembg and is separate from Chronon's render dependencies.
    session = new_session(args.model)
    predicted = remove(image, session=session, only_mask=True,
                       post_process_mask=args.post_process)
    matte = predicted.convert("L")
    if matte.size != image.size:
        matte = matte.resize(image.size, Image.Resampling.BILINEAR)

    source_alpha = image.getchannel("A")
    if source_alpha.getextrema() != (255, 255):
        import PIL.ImageChops
        matte = PIL.ImageChops.multiply(matte, source_alpha)

    matte_values = np.asarray(matte, dtype=np.float32) / 255.0
    alpha_values = np.asarray(source_alpha, dtype=np.uint8)
    visible = alpha_values > 0
    visible_count = int(np.count_nonzero(visible))
    if visible_count == 0:
        foreground_fraction = 0.0
        mean_uncertainty = 0.0
    else:
        foreground_fraction = float(np.count_nonzero((matte_values >= 0.5) & visible) / visible_count)
        uncertainty = 4.0 * matte_values * (1.0 - matte_values)
        mean_uncertainty = float(np.mean(uncertainty[visible]))

    accepted = (args.minimum_foreground_fraction <= foreground_fraction <= args.maximum_foreground_fraction
                and mean_uncertainty <= args.maximum_mean_uncertainty)
    record = {
        "schema": "chronon.semantic_subject_matte.v1",
        "provider": "rembg/onnxruntime",
        "model": args.model,
        "input": str(args.input.resolve()),
        "input_sha256": hashlib.sha256(input_bytes).hexdigest(),
        "width": width,
        "height": height,
        "foreground_fraction": foreground_fraction,
        "mean_uncertainty": mean_uncertainty,
        "uncertainty_definition": "mean(4*a*(1-a)) over source-visible pixels",
        "accepted": accepted,
        "output": str(args.output.resolve()),
        "output_written": False,
    }
    if not accepted:
        record["rejection"] = "foreground_fraction_or_uncertainty_outside_gate"
        atomic_json(report_path, record)
        return 2

    args.output.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=f".{args.output.stem}.", suffix=".png", dir=args.output.parent)
    os.close(fd)
    try:
        matte.save(temporary, format="PNG")
        record["matte_sha256"] = hashlib.sha256(Path(temporary).read_bytes()).hexdigest()
        atomic_json(report_path, record)
        os.replace(temporary, args.output)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
    record["output_written"] = True
    atomic_json(report_path, record)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
