#!/usr/bin/env python3
"""The Motion3D canary harness.

Renders the `Motion3D*` canary compositions (registered by
`apps/chronon3d_cli/register_canary_compositions.cpp`) and measures them, so a
claim about the motion model can be checked instead of believed:

  goldens        raw pre-encode frames at the golden frames, hashed (the raw
                 RGBA the encoder consumes — the CLI's PNG lane is explicitly
                 NOT a pixel-truth surface for text layers)
  determinism    the same frames asked for out of order, and one frame ten
                 times, must be bit-identical to the sequential pass
  motion-dump    per-TextRun world matrices + predicted bboxes per golden frame
  same-motion    a text layer and an image layer carrying the same authored
                 transform must trace the same projected trajectory
  benchmark      phase attribution (startup / prepare / render / encode), wall
                 time and peak RSS, from the engine's own sidecars
  video          the hero clip as 1920x1080 30 fps MP4

Everything is read back from files the engine wrote; nothing here re-derives a
matrix, so a disagreement is the engine's, not the harness's.

Usage:
    python3 tools/motion3d_canary.py all --out output/canary3d
    python3 tools/motion3d_canary.py goldens --clip Motion3DGlowOrbit
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import time
import zlib
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_CLI = REPO_ROOT / "build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli"

WIDTH = 1920
HEIGHT = 1080
FRAME_BYTES = WIDTH * HEIGHT * 4

# The repository's own raw-frame hasher.  The canary does not re-implement the
# manifest format: it asks this tool for the hashes, so a machine gate that runs
# the tool on our own artifacts sees exactly the bytes we recorded.
RAW_FRAME_HASH = REPO_ROOT / "tools/raw_frame_hash.py"
FRAMEHASH_LINE = re.compile(r"^frame=(?P<frame>\d+) sha256=(?P<sha>[0-9a-f]{64})$", re.MULTILINE)

GOLDEN_FRAMES = [0, 15, 30, 45, 60, 75, 89]
HERO_CLIP = "Motion3DGlowOrbit"
OUT_OF_ORDER = [60, 1, 45, 0, 89, 30, 60]
REPEAT_FRAME = 30
REPEAT_COUNT = 10

CANARY_CLIPS = [
    "Motion3DGlowOrbit",
    "Motion3DSentenceOrbit",
    "Motion3DTextParentCamera",
    "Motion3DSameMotionText",
    "Motion3DSameMotionImage",
]

SAME_MOTION_TEXT = "Motion3DSameMotionText"
SAME_MOTION_IMAGE = "Motion3DSameMotionImage"

# The background the compositions author; used only to separate ink from void
# when a projected box is measured.  Kept in one place so a palette change
# shows up as a harness failure rather than a silently wrong bbox.
BACKGROUND = np.array([9, 11, 19], dtype=np.int16)
INK_THRESHOLD = 18

_ENGINE_SHA: str | None = None


# ── process plumbing ────────────────────────────────────────────────────────


@dataclass
class RunResult:
    cmd: list[str]
    wall_ms: float
    returncode: int
    stdout: str
    stderr: str
    peak_rss_kb: int


def _vm_hwm_kb(pid: int) -> int:
    """The child's own peak RSS (VmHWM), read from its /proc entry.

    `getrusage(RUSAGE_CHILDREN)` is monotonic across every child this harness
    ever spawns, so it reports one process's high-water mark for all of them —
    useless per render.  This is the per-process number.
    """
    try:
        with open(f"/proc/{pid}/status", "r") as handle:
            for line in handle:
                if line.startswith("VmHWM:"):
                    return int(line.split()[1])
    except OSError:
        pass
    return 0


def run_cli(cli: Path, args: list[str], log_level: str = "warn") -> RunResult:
    cmd = [str(cli), *args, "-v", log_level]
    started = time.monotonic()
    proc = subprocess.Popen(cmd, cwd=str(REPO_ROOT),
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    peak_kb = 0
    while proc.poll() is None:
        peak_kb = max(peak_kb, _vm_hwm_kb(proc.pid))
        time.sleep(0.05)
    stdout, stderr = proc.communicate()
    wall_ms = (time.monotonic() - started) * 1000.0
    peak_kb = max(peak_kb, _vm_hwm_kb(proc.pid))
    return RunResult(
        cmd=cmd,
        wall_ms=wall_ms,
        returncode=proc.returncode,
        stdout=stdout,
        stderr=stderr,
        peak_rss_kb=peak_kb,
    )


def render_raw(cli: Path, clip: str, frames: str, out_file: Path,
               extra: list[str] | None = None, log_level: str = "warn") -> RunResult:
    """Render `frames` through the raw pre-encode sink (the pixel-truth lane)."""
    out_file.parent.mkdir(parents=True, exist_ok=True)
    args = [
        "render", clip,
        "--frames", frames,
        "--video-sink", "raw",
        "-o", str(out_file),
        "--assets-root", str(REPO_ROOT),
        "--backend", "software",
    ]
    if extra:
        args.extend(extra)
    return run_cli(cli, args, log_level=log_level)


def sha256_of(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def slice_frame(path: Path, frame: int) -> bytes:
    with path.open("rb") as handle:
        handle.seek(frame * FRAME_BYTES)
        data = handle.read(FRAME_BYTES)
    if len(data) != FRAME_BYTES:
        raise RuntimeError(f"{path}: frame {frame} truncated ({len(data)} of {FRAME_BYTES} bytes)")
    return data


def engine_sha() -> str:
    """The commit the canary artifacts were produced from, when it is known."""
    global _ENGINE_SHA
    if _ENGINE_SHA is None:
        try:
            proc = subprocess.run(["git", "-C", str(REPO_ROOT), "rev-parse", "HEAD"],
                                  capture_output=True, text=True, timeout=15)
            _ENGINE_SHA = proc.stdout.strip() if proc.returncode == 0 else "unknown"
        except (OSError, subprocess.SubprocessError):
            _ENGINE_SHA = "unknown"
    return _ENGINE_SHA or "unknown"


def framehash_manifest(raw: Path, plan_digest: str = "unknown", out: Path | None = None) -> str:
    """The canonical CHRONON FRAMEHASH v1 manifest for a raw sink file."""
    cmd = [sys.executable, str(RAW_FRAME_HASH), "--raw", str(raw),
           "--width", str(WIDTH), "--height", str(HEIGHT),
           "--engine-sha", engine_sha(), "--plan-digest", plan_digest]
    if out is not None:
        out.parent.mkdir(parents=True, exist_ok=True)
        cmd += ["--out", str(out)]
    proc = subprocess.run(cmd, capture_output=True, text=True, cwd=str(REPO_ROOT))
    if proc.returncode != 0:
        raise RuntimeError(f"raw_frame_hash failed on {raw}: {proc.stderr.strip()}")
    return out.read_text() if out is not None else proc.stdout


def run_raw_frame_hash_gate(raw: Path, golden: Path) -> dict:
    """Run `tools/raw_frame_hash.py --golden`, the repository's own gate."""
    if not golden.exists():
        return {"passes": False, "detail": f"golden manifest missing: {golden}"}
    # The manifest header is provenance (which engine sha wrote the golden), and
    # the frame lines are pixels.  The gate compares the whole file, so it has
    # to declare the golden's own engine sha and the current one is reported
    # separately — a moved HEAD must not be reported as a pixel difference, and
    # a pixel difference must not hide behind a header mismatch.
    recorded_sha = "unknown"
    for line in golden.read_text().splitlines():
        if line.startswith("engine_sha="):
            recorded_sha = line.split("=", 1)[1]
            break
    proc = subprocess.run(
        [sys.executable, str(RAW_FRAME_HASH), "--raw", str(raw),
         "--width", str(WIDTH), "--height", str(HEIGHT),
         "--engine-sha", recorded_sha, "--golden", str(golden)],
        capture_output=True, text=True, cwd=str(REPO_ROOT))
    return {
        "passes": proc.returncode == 0,
        "golden": str(golden.relative_to(golden.parent.parent))
        if golden.parent.parent != golden.parent else str(golden.name),
        "golden_engine_sha": recorded_sha,
        "current_engine_sha": engine_sha(),
        "detail": "FRAMEHASH_PASS" if proc.returncode == 0 else proc.stderr.strip()[-800:],
    }


def frame_hashes(path: Path, frames: list[int]) -> dict[str, str]:
    """Per-frame SHA-256 straight from the repository's raw hasher."""
    with tempfile.TemporaryDirectory() as directory:
        manifest = framehash_manifest(path, out=Path(directory) / "frames.framehash")
    table = {match.group("frame"): match.group("sha") for match in FRAMEHASH_LINE.finditer(manifest)}
    missing = [frame for frame in frames if str(frame) not in table]
    if missing:
        raise RuntimeError(f"{path}: no hash recorded for frames {missing}")
    return {str(frame): table[str(frame)] for frame in frames}


def decode_frame(data: bytes) -> np.ndarray:
    return np.frombuffer(data, dtype=np.uint8).reshape(HEIGHT, WIDTH, 4)


# ── pixel facts (no engine involvement) ─────────────────────────────────────


@dataclass
class InkStats:
    pixels: int
    bbox: tuple[int, int, int, int] | None
    centroid: tuple[float, float] | None
    size: tuple[int, int] | None

    def as_json(self) -> dict:
        return {
            "pixels": self.pixels,
            "bbox": list(self.bbox) if self.bbox else None,
            "centroid": [round(self.centroid[0], 3), round(self.centroid[1], 3)] if self.centroid else None,
            "size": list(self.size) if self.size else None,
        }


def ink_stats(data: bytes) -> InkStats:
    rgba = decode_frame(data)
    rgb = rgba[:, :, :3].astype(np.int16)
    diff = np.abs(rgb - BACKGROUND).sum(axis=2)
    mask = diff > INK_THRESHOLD
    pixels = int(mask.sum())
    if pixels == 0:
        return InkStats(0, None, None, None)
    ys, xs = np.nonzero(mask)
    bbox = (int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max()))
    centroid = (float(xs.mean()), float(ys.mean()))
    size = (bbox[2] - bbox[0] + 1, bbox[3] - bbox[1] + 1)
    return InkStats(pixels, bbox, centroid, size)


def write_png(path: Path, frame: np.ndarray) -> None:
    """A minimal, deterministic PNG writer: the same pixels always give the
    same bytes, so a golden PNG can be hashed as well as looked at."""
    rgb = np.ascontiguousarray(frame[:, :, :3])
    height, width, _ = rgb.shape
    raw = bytearray()
    stride = width * 3
    flat = rgb.tobytes()
    for y in range(height):
        raw.append(0)  # filter type 0 (None)
        raw += flat[y * stride:(y + 1) * stride]

    def chunk(tag: bytes, payload: bytes) -> bytes:
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as handle:
        handle.write(b"\x89PNG\r\n\x1a\n")
        handle.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        handle.write(chunk(b"IDAT", zlib.compress(bytes(raw), 6)))
        handle.write(chunk(b"IEND", b""))


# ── sidecar reading ─────────────────────────────────────────────────────────


def load_json(path: Path) -> dict:
    try:
        return json.loads(path.read_text())
    except Exception:
        return {}


def dig(data: dict, *keys, default=None):
    node = data
    for key in keys:
        if not isinstance(node, dict) or key not in node:
            return default
        node = node[key]
    return node


def benchmark_from_sidecar(timing_path: Path, wall_ms: float, peak_rss_kb: int) -> dict:
    timing = load_json(timing_path)
    telemetry = load_json(Path(str(timing_path).replace(".timing.json", ".telemetry-summary.json")))
    cpu = dig(timing, "job", "cpu_breakdown", default={}) or {}
    return {
        "frames": timing.get("frames_total"),
        "wall_ms_measured": round(wall_ms, 3),
        "wall_ms_engine": timing.get("wall_time_ms"),
        "peak_rss_mb": round(peak_rss_kb / 1024.0, 2),
        "phases_engine": dig(timing, "phase_attribution", "phases", default={}) or {},
        "prepare": dig(timing, "prepare_breakdown", default={}) or {},
        "startup": dig(timing, "startup_breakdown", default={}) or {},
        "render_ms": timing.get("render_ms"),
        "avg_frame_ms": dig(timing, "summary", "avg_frame_ms"),
        "end_to_end_fps": dig(timing, "summary", "end_to_end_fps"),
        "graph": {
            "graph_build_ms": cpu.get("graph_build_ms"),
            "graph_execute_ms": cpu.get("graph_execute_ms"),
            "node_execute_actual_ms": cpu.get("node_execute_actual_ms"),
            "effect_stack_total_ms": cpu.get("effect_stack_total_ms"),
            "text_layout_ms": cpu.get("text_layout_ms"),
            "text_shaping_ms": cpu.get("text_shaping_ms"),
            "text_rasterization_ms": cpu.get("text_rasterization_ms"),
            "text_draw_ms": cpu.get("text_draw_ms"),
            "predicted_bbox_ms": cpu.get("predicted_bbox_ms"),
        },
        "encoder": dig(timing, "job", "encoder", default={}) or {},
        "memory": dig(timing, "memory", default={}) or {},
        "cache": timing.get("cache", {}),
        "backend": dig(telemetry, "job", "backend", default=None),
    }


# ── commands ────────────────────────────────────────────────────────────────


@dataclass
class Context:
    cli: Path
    out: Path
    work: Path
    clips: list[str]
    report: dict = field(default_factory=dict)

    def cleanup(self) -> None:
        shutil.rmtree(self.work, ignore_errors=True)


def cmd_goldens(ctx: Context) -> None:
    """Raw pre-encode goldens + human-readable PNGs + per-frame hashes."""
    result = {}
    for clip in ctx.clips:
        raw = ctx.work / f"{clip}_goldens.raw"
        frames = ",".join(str(f) for f in GOLDEN_FRAMES)
        # One process per clip for the whole range would be cheapest, but the
        # canary asks for these seven frames specifically; a single sequential
        # pass over 0-89 is both cheaper and gives the determinism baseline.
        run = render_raw(ctx.cli, clip, "0-89", raw)
        if run.returncode != 0:
            raise SystemExit(f"render failed for {clip}: {run.stderr[-2000:]}")
        hashes = frame_hashes(raw, GOLDEN_FRAMES)
        golden_dir = ctx.out / "goldens" / clip
        for frame in GOLDEN_FRAMES:
            png = golden_dir / f"frame_{frame:05d}.png"
            write_png(png, decode_frame(slice_frame(raw, frame)))
        # The repository's machine gate: `tools/raw_frame_hash.py --golden` reads
        # this file back, so the recorded goldens are checkable without us.
        manifest_path = ctx.out / "framehash" / f"{clip}.framehash"
        framehash_manifest(raw, out=manifest_path)
        (golden_dir / "hashes.json").write_text(json.dumps(
            {
                "clip": clip,
                "surface": "raw pre-encode RGBA8, 1920x1080, --video-sink raw",
                "sequential_pass_frames": "0-89",
                "golden_frames": GOLDEN_FRAMES,
                "engine_sha": engine_sha(),
                "framehash_manifest": str(manifest_path.relative_to(ctx.out)),
                "sha256": hashes,
            }, indent=2) + "\n")
        result[clip] = hashes
        # The sequential hashes are the determinism baseline for the hero.
        if clip == HERO_CLIP:
            (ctx.out / "determinism").mkdir(parents=True, exist_ok=True)
            (ctx.out / "benchmark").mkdir(parents=True, exist_ok=True)
            (ctx.out / "determinism" / "hero_sequential_hashes.json").write_text(
                json.dumps({"frames": "0-89", "sha256": frame_hashes(raw, list(range(90)))}, indent=2) + "\n")
            shutil.copyfile(raw.with_suffix(raw.suffix + ".timing.json"),
                            ctx.out / "benchmark" / f"{clip}_raw.timing.json")
        raw.unlink(missing_ok=True)
        raw.with_suffix(raw.suffix + ".timing.json").unlink(missing_ok=True)
        raw.with_suffix(raw.suffix + ".telemetry-summary.json").unlink(missing_ok=True)
    ctx.report["goldens"] = result


def cmd_determinism(ctx: Context) -> None:
    """Out-of-order access and repeated frames, against the sequential baseline."""
    baseline_path = ctx.out / "determinism" / "hero_sequential_hashes.json"
    if not baseline_path.exists():
        raise SystemExit("run `goldens` first: the sequential baseline is missing")
    baseline = load_json(baseline_path)["sha256"]

    out_dir = ctx.out / "determinism"
    out_dir.mkdir(parents=True, exist_ok=True)

    out_of_order = []
    for position, frame in enumerate(OUT_OF_ORDER):
        raw = ctx.work / f"oo_{position:02d}.raw"
        run = render_raw(ctx.cli, HERO_CLIP, str(frame), raw)
        if run.returncode != 0:
            raise SystemExit(f"out-of-order render of frame {frame} failed: {run.stderr[-2000:]}")
        digest = sha256_of(slice_frame(raw, 0))
        out_of_order.append({
            "request_position": position,
            "frame": frame,
            "sha256": digest,
            "sequential_sha256": baseline[str(frame)],
            "bit_identical": digest == baseline[str(frame)],
        })
        raw.unlink(missing_ok=True)

    repeats = []
    for attempt in range(REPEAT_COUNT):
        raw = ctx.work / f"repeat_{attempt:02d}.raw"
        run = render_raw(ctx.cli, HERO_CLIP, str(REPEAT_FRAME), raw)
        if run.returncode != 0:
            raise SystemExit(f"repeat render {attempt} failed: {run.stderr[-2000:]}")
        repeats.append(sha256_of(slice_frame(raw, 0)))
        raw.unlink(missing_ok=True)

    summary = {
        "clip": HERO_CLIP,
        "surface": "raw pre-encode RGBA8",
        "out_of_order": {
            "sequence": OUT_OF_ORDER,
            "results": out_of_order,
            "all_bit_identical": all(item["bit_identical"] for item in out_of_order),
        },
        "repeated_frame": {
            "frame": REPEAT_FRAME,
            "runs": REPEAT_COUNT,
            "sha256": repeats,
            "distinct": sorted(set(repeats)),
            "all_bit_identical": len(set(repeats)) == 1,
        },
    }
    (out_dir / "hero_determinism.json").write_text(json.dumps(summary, indent=2) + "\n")
    ctx.report["determinism"] = summary


# The graph preflight table (`--diagnostic-plan --diagnostic-plan-output`): one
# row per node, with the **projected** bbox the renderer will composite.  This
# is the per-layer geometry dump: it covers text runs and image sources alike,
# and it is emitted by the production CLI.
PREFLIGHT_ROW = re.compile(
    r"^(?P<name>\S+)\s+(?P<kind>[A-Za-z]+)\s+"
    r"\[(?P<b0>-?\d+),(?P<b1>-?\d+),(?P<b2>-?\d+),(?P<b3>-?\d+)\]\s+"
    r"\[(?P<i0>-?\d+),(?P<i1>-?\d+),(?P<i2>-?\d+),(?P<i3>-?\d+)\]\s+"
    r"(?P<vis_ratio>[\d.]+)\s+(?P<visibility>\S+)\s+"
    r"(?P<dirty>YES|no)\s+(?P<cached>YES|no)\s+(?P<memory>\S+\s*\S*?)\s{2,}(?P<complexity>\d+)\s*$"
)


def parse_preflight_report(text: str) -> dict:
    """The engine's graph preflight report as structured data."""
    header: dict = {}
    nodes: list[dict] = []
    warnings: list[str] = []
    in_table = False
    for line in text.splitlines():
        if line.startswith("-----"):
            in_table = True
            continue
        if line.startswith("--- WARNINGS"):
            in_table = False
            continue
        if line.strip().startswith("[") and "WARN" in line:
            warnings.append(line.strip())
            continue
        for key, pattern in (("nodes", r"^Nodes:\s*(\d+)"),
                             ("warnings", r"^Warnings:\s*(\d+)"),
                             ("peak_memory", r"^Peak Memory:\s*(.+?)\s*$"),
                             ("total_complexity", r"^Total Complexity:\s*(\d+)"),
                             ("cache_score", r"^Cache Score:\s*(\d+)%"),
                             ("total_fill_rate_pixels", r"^Total Fill Rate:\s*(\d+)")):
            match = re.match(pattern, line)
            if match:
                header[key] = match.group(1)
        if not in_table:
            continue
        match = PREFLIGHT_ROW.match(line.rstrip())
        if not match:
            continue
        row = match.groupdict()
        nodes.append({
            "name": row["name"],
            "kind": row["kind"],
            "predicted_bbox": [int(row[f"b{i}"]) for i in range(4)],
            "intersection_bbox": [int(row[f"i{i}"]) for i in range(4)],
            "visible_ratio": float(row["vis_ratio"]),
            "visibility": row["visibility"],
            "dirty": row["dirty"] == "YES",
            "cached": row["cached"] == "YES",
            "memory": row["memory"].strip(),
            "complexity": int(row["complexity"]),
        })
    # Deliberately NOT paired into per-layer "projected box" records: the
    # `Transform` row that follows a source row is not reliably the same
    # layer's, and a plausible-looking wrong pairing is worse than no pairing.
    # The rows are kept verbatim; the assertions in this harness come from the
    # pixels, and this table is what explains them.
    return {"header": header, "nodes": nodes, "warnings": warnings}


def bbox_center(bbox: list[int]) -> tuple[float, float]:
    return ((bbox[0] + bbox[2]) / 2.0, (bbox[1] + bbox[3]) / 2.0)


def cmd_motion_dump(ctx: Context) -> None:
    """Per-layer projected geometry + the pixels that should match it.

    Two independent records per golden frame: the engine's graph preflight
    report (each node's projected bbox in canvas space, for text runs and image
    sources alike) and the raw frame the encoder consumed.  A frame whose bbox
    trajectory is wrong is a motion problem; one whose bbox is right and whose
    pixels are wrong is a raster/composite problem.
    """
    dump_dir = ctx.out / "motion_dump"
    dump_dir.mkdir(parents=True, exist_ok=True)
    result = {}
    for clip in ctx.clips:
        clip_dir = dump_dir / clip
        clip_dir.mkdir(parents=True, exist_ok=True)
        raw = ctx.work / f"{clip}_dump.raw"
        frames: dict = {}
        for frame in GOLDEN_FRAMES:
            pattern = clip_dir / f"preflight_{frame:05d}.txt"
            run = render_raw(ctx.cli, clip, str(frame), raw, extra=[
                "--diagnostic-plan",
                "--diagnostic-plan-output", str(pattern),
            ])
            if run.returncode != 0:
                raise SystemExit(f"motion dump failed for {clip} frame {frame}: {run.stderr[-2000:]}")
            # The engine appends the frame it actually rendered to the pattern
            # stem: a random-access render pre-rolls frame 0, so asking for
            # frame 30 writes `preflight_00030_0000.txt` (the pre-roll) and
            # `preflight_00030_0030.txt` (the frame we asked for).  The file
            # whose trailing number is the requested frame is the one to keep;
            # if it is missing we say so instead of recording the pre-roll.
            produced = sorted(clip_dir.glob("preflight_*.txt"),
                              key=lambda path: int(path.stem.split("_")[-1]))
            wanted = [path for path in produced if int(path.stem.split("_")[-1]) == frame]
            if not wanted:
                raise SystemExit(
                    f"{clip} frame {frame}: the diagnostic plan was not written for "
                    f"that frame; files produced: {[p.name for p in produced]}")
            text = wanted[-1].read_text()
            for path in produced:
                path.unlink()
            (clip_dir / f"preflight_{frame:05d}.txt").write_text(text)
            report = parse_preflight_report(text)
            # A single-frame render writes exactly one frame: slice at 0, not at
            # the requested index.
            stats = ink_stats(slice_frame(raw, 0))
            frames[str(frame)] = {
                "pixels": stats.as_json(),
                "preflight": report,
            }
        raw.unlink(missing_ok=True)
        result[clip] = {"frames": [str(f) for f in GOLDEN_FRAMES], "by_frame": frames}

    (dump_dir / "motion_dump.json").write_text(json.dumps({
        "note": (
            "per golden frame: the engine's graph preflight report (one row per "
            "graph node with its projected bbox in canvas space) and the ink bbox "
            "measured from the raw pre-encode frame.  Read together they separate "
            "a motion problem from a raster problem: a frame whose geometry rows "
            "are right but whose pixels are wrong is a compositor issue."
        ),
        "clips": result,
    }, indent=2) + "\n")
    ctx.report["motion_dump"] = {
        clip: {
            "frames": entry["frames"],
            "nodes_per_frame": [len(entry["by_frame"][f]["preflight"]["nodes"]) for f in entry["frames"]],
        }
        for clip, entry in result.items()
    }


def cmd_same_motion(ctx: Context) -> None:
    """The text layer and the image layer must trace the same trajectory."""
    trajectories = {}
    for clip in (SAME_MOTION_TEXT, SAME_MOTION_IMAGE):
        raw = ctx.work / f"{clip}_traj.raw"
        run = render_raw(ctx.cli, clip, "0-89", raw)
        if run.returncode != 0:
            raise SystemExit(f"trajectory render failed for {clip}: {run.stderr[-2000:]}")
        trajectory = []
        for frame in range(90):
            stats = ink_stats(slice_frame(raw, frame))
            trajectory.append({"frame": frame, **stats.as_json()})
        trajectories[clip] = trajectory
        raw.unlink(missing_ok=True)

    text_traj = trajectories[SAME_MOTION_TEXT]
    image_traj = trajectories[SAME_MOTION_IMAGE]

    rows = []
    for text_row, image_row in zip(text_traj, image_traj):
        if text_row["centroid"] is None or image_row["centroid"] is None:
            rows.append({"frame": text_row["frame"], "comparable": False})
            continue
        rows.append({
            "frame": text_row["frame"],
            "comparable": True,
            "centroid": text_row["centroid"],
            "centroid_image": image_row["centroid"],
            "size": text_row["size"],
            "size_image": image_row["size"],
            "centroid_delta": [
                round(text_row["centroid"][0] - image_row["centroid"][0], 3),
                round(text_row["centroid"][1] - image_row["centroid"][1], 3),
            ],
            "size_ratio": [
                round(text_row["size"][0] / max(1, image_row["size"][0]), 5),
                round(text_row["size"][1] / max(1, image_row["size"][1]), 5),
            ],
        })

    comparable = [row for row in rows if row["comparable"]]
    dx = [row["centroid_delta"][0] for row in comparable]
    dy = [row["centroid_delta"][1] for row in comparable]
    rx = [row["size_ratio"][0] for row in comparable]
    ry = [row["size_ratio"][1] for row in comparable]

    # What the pixels can actually decide here.  A glyph run's ink sits inside
    # its box, and the inset rotates with the layer, so the text and image ink
    # boxes are NOT the same rectangle at the same instant — a constant offset
    # would be too strong a claim.  What is decidable is the screen trajectory:
    # under one authored transform, the two content kinds must sweep the frame
    # the same way, and the residual is bounded by the ink inset (at most half
    # the box, i.e. half its diagonal).
    first = comparable[0]
    last = comparable[-1]
    text_motion = [last["centroid"][0] - first["centroid"][0], last["centroid"][1] - first["centroid"][1]]
    image_motion = [last["centroid_image"][0] - first["centroid_image"][0],
                    last["centroid_image"][1] - first["centroid_image"][1]]
    motion_delta = [round(text_motion[0] - image_motion[0], 2), round(text_motion[1] - image_motion[1], 2)]
    motion_delta_len = (motion_delta[0] ** 2 + motion_delta[1] ** 2) ** 0.5
    box_diagonal = (880.0 ** 2 + 420.0 ** 2) ** 0.5
    inset_bound = 0.25 * box_diagonal  # a conservative share of the ink inset

    summary = {
        "claim": (
            "a text layer and an image layer carrying the same authored transform "
            "(position, rotation, scale, parent, camera) sweep the frame along the "
            "same trajectory; the content kind is not an input to the transform"
        ),
        "method": (
            "both clips are rendered 0-89 raw and the ink bbox/centroid is measured "
            "per frame from the pixels.  The two trajectories are compared by "
            "displacement over the clip, not by absolute bbox: a glyph's ink is "
            "inset inside its box and that inset rotates with the layer, so the "
            "two boxes are not the same rectangle at the same instant, while the "
            "motion they describe is the same motion."
        ),
        "what_this_cannot_prove": (
            "it does not prove the two world matrices are bit-identical — that is "
            "a property of the engine's single transform path, not something an ink "
            "bbox can witness.  It bounds the difference."
        ),
        "frames_compared": len(comparable),
        "text_motion_px": [round(text_motion[0], 2), round(text_motion[1], 2)],
        "image_motion_px": [round(image_motion[0], 2), round(image_motion[1], 2)],
        "motion_delta_px": motion_delta,
        "motion_delta_len_px": round(motion_delta_len, 2),
        "authored_box_diagonal_px": round(box_diagonal, 2),
        "inset_bound_px": round(inset_bound, 2),
        "passes": motion_delta_len <= inset_bound,
        "centroid_delta_x": {"min": min(dx), "max": max(dx), "span": round(max(dx) - min(dx), 3)},
        "centroid_delta_y": {"min": min(dy), "max": max(dy), "span": round(max(dy) - min(dy), 3)},
        "size_ratio_x": {"min": min(rx), "max": max(rx), "span": round(max(rx) - min(rx), 6)},
        "size_ratio_y": {"min": min(ry), "max": max(ry), "span": round(max(ry) - min(ry), 6)},
        "per_frame": rows,
    }
    # Side by side for the reader: the engine's own node table for both clips at
    # each golden frame.  No assertion is drawn from it — a text run's row is its
    # ink bounds and an image's is its quad, so the two tables are not comparable
    # value for value.  The pixel trajectory above is the test; this is the
    # engine-side record next to it, and the full table lives in the motion dump.
    dump_path = ctx.out / "motion_dump" / "motion_dump.json"
    if dump_path.exists():
        dump = load_json(dump_path).get("clips", {})
        text_clip = dump.get(SAME_MOTION_TEXT, {}).get("by_frame", {})
        image_clip = dump.get(SAME_MOTION_IMAGE, {}).get("by_frame", {})
        summary["engine_node_rows"] = {
            str(frame): {
                "text_clip": text_clip.get(str(frame), {}).get("preflight", {}).get("nodes", []),
                "image_clip": image_clip.get(str(frame), {}).get("preflight", {}).get("nodes", []),
            }
            for frame in GOLDEN_FRAMES
        }

    out_dir = ctx.out / "same_motion"
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "same_motion.json").write_text(json.dumps(summary, indent=2) + "\n")
    with (out_dir / "trajectories.csv").open("w") as handle:
        handle.write("clip,frame,pixels,x0,y0,x1,y1,cx,cy,w,h\n")
        for clip, trajectory in trajectories.items():
            for row in trajectory:
                bbox = row["bbox"] or [None] * 4
                size = row["size"] or [None] * 2
                centroid = row["centroid"] or [None, None]
                handle.write(",".join(str(v) for v in [
                    clip, row["frame"], row["pixels"], *bbox, *centroid, *size,
                ]) + "\n")
    ctx.report["same_motion"] = {k: v for k, v in summary.items() if k != "per_frame"}


def cmd_benchmark(ctx: Context) -> None:
    """Phase attribution for the raw pass and the encoded pass of every clip."""
    results = {}
    bench_dir = ctx.out / "benchmark"
    bench_dir.mkdir(parents=True, exist_ok=True)
    for clip in ctx.clips:
        raw = ctx.work / f"{clip}_bench.raw"
        raw_run = render_raw(ctx.cli, clip, "0-89", raw)
        if raw_run.returncode != 0:
            raise SystemExit(f"benchmark raw render failed for {clip}: {raw_run.stderr[-2000:]}")
        raw_bench = benchmark_from_sidecar(
            Path(str(raw) + ".timing.json"), raw_run.wall_ms, raw_run.peak_rss_kb)
        raw_bench["lane"] = "raw pre-encode (no encoder)"
        shutil.copyfile(Path(str(raw) + ".timing.json"), bench_dir / f"{clip}_raw.timing.json")
        raw.unlink(missing_ok=True)
        Path(str(raw) + ".timing.json").unlink(missing_ok=True)
        Path(str(raw) + ".telemetry-summary.json").unlink(missing_ok=True)

        video = ctx.out / "mp4" / f"{clip}_1920x1080_30fps_3s.mp4"
        video.parent.mkdir(parents=True, exist_ok=True)
        video_run = run_cli(ctx.cli, [
            "render", clip,
            "--frames", "0-89",
            "-o", str(video),
            "--assets-root", str(REPO_ROOT),
            "--backend", "software",
        ])
        if video_run.returncode != 0:
            raise SystemExit(f"benchmark video render failed for {clip}: {video_run.stderr[-2000:]}")
        video_bench = benchmark_from_sidecar(
            video.with_suffix(video.suffix + ".timing.json"), video_run.wall_ms, video_run.peak_rss_kb)
        video_bench["lane"] = "h264 video (encoded)"
        video_bench["output_bytes"] = video.stat().st_size
        video_bench["output_sha256"] = sha256_of(video.read_bytes())
        timing_sidecar = video.with_suffix(video.suffix + ".timing.json")
        if timing_sidecar.exists():
            shutil.copyfile(timing_sidecar, bench_dir / f"{clip}_video.timing.json")

        results[clip] = {
            "frames": 90,
            "width": WIDTH,
            "height": HEIGHT,
            "fps": 30,
            "raw": raw_bench,
            "video": video_bench,
        }
    (bench_dir / "benchmark.json").write_text(json.dumps(results, indent=2) + "\n")
    ctx.report["benchmark"] = results


def cmd_recheck(ctx: Context) -> None:
    """Re-render every golden frame and compare it with the recorded hashes.

    Run against a *freshly built* binary this is a build-reproducibility check
    as much as a render check: the same scene, the same frames, another
    executable, the same bytes.
    """
    recorded = ctx.report.get("goldens")
    if not recorded:
        raise SystemExit("run `goldens` first: there is nothing to re-check against")

    result: dict = {"clips": {}, "all_match": True}
    for clip in ctx.clips:
        raw = ctx.work / f"{clip}_recheck.raw"
        run = render_raw(ctx.cli, clip, "0-89", raw)
        if run.returncode != 0:
            raise SystemExit(f"recheck render failed for {clip}: {run.stderr[-2000:]}")
        fresh = frame_hashes(raw, GOLDEN_FRAMES)
        expected = recorded.get(clip, {})
        mismatches = [frame for frame in GOLDEN_FRAMES
                      if expected.get(str(frame)) != fresh[str(frame)]]
        # Independent confirmation: let the repository's own gate compare the
        # fresh raw file against the golden manifest it wrote earlier.
        golden_manifest = ctx.out / "framehash" / f"{clip}.framehash"
        gate = run_raw_frame_hash_gate(raw, golden_manifest)
        entry = {
            "golden_frames_checked": len(GOLDEN_FRAMES),
            "mismatches": mismatches,
            "tool_gate": gate,
            "sha256": fresh,
        }
        if clip == HERO_CLIP:
            sequential = load_json(ctx.out / "determinism" / "hero_sequential_hashes.json").get("sha256", {})
            full = frame_hashes(raw, list(range(90)))
            entry["all_90_frames_checked"] = len(sequential)
            entry["all_90_frames_mismatches"] = [int(f) for f in sorted(sequential, key=int)
                                                 if sequential[f] != full.get(f)]
            result["all_match"] = result["all_match"] and not entry["all_90_frames_mismatches"]
        result["clips"][clip] = entry
        result["all_match"] = result["all_match"] and not mismatches
        result["all_match"] = result["all_match"] and gate["passes"]
        raw.unlink(missing_ok=True)
        raw.with_suffix(raw.suffix + ".timing.json").unlink(missing_ok=True)
        raw.with_suffix(raw.suffix + ".telemetry-summary.json").unlink(missing_ok=True)

    (ctx.out / "recheck").mkdir(parents=True, exist_ok=True)
    (ctx.out / "recheck" / "rebuild_recheck.json").write_text(json.dumps(result, indent=2) + "\n")
    ctx.report["recheck"] = {clip: {k: v for k, v in entry.items() if k != "sha256"}
                             for clip, entry in result["clips"].items()}
    ctx.report["recheck"]["all_match"] = result["all_match"]
    ctx.report["recheck"]["raw_frame_hash_gate"] = "tools/raw_frame_hash.py --golden"


def cmd_package(ctx: Context) -> None:
    """Assemble the upload set: what a human watches, what a machine checks, and
    one tarball with everything for reproducing the run."""
    upload = ctx.out / "upload"
    shutil.rmtree(upload, ignore_errors=True)
    upload.mkdir(parents=True, exist_ok=True)

    for mp4 in sorted((ctx.out / "mp4").glob("*.mp4")):
        shutil.copy2(mp4, upload / mp4.name)
    report = ctx.out / "motion3d_canary_report.md"
    if report.exists():
        shutil.copy2(report, upload / report.name)

    # The consolidated machine-readable evidence: exactly the state the report
    # was rendered from, plus the digests of the artefacts being shipped.
    evidence = dict(ctx.report)
    evidence["clips"] = CANARY_CLIPS
    evidence["golden_frames"] = GOLDEN_FRAMES
    evidence["observations"] = [
        {
            "what": "one re-check pass reported Motion3DGlowOrbit frame 22 differing from "
                    "the sequential baseline",
            "status": "observed once, not reproduced; raw bytes not kept",
            "attempted": "12 isolated re-renders of frame 22, 3 full 0-89 passes and the "
                         "final re-check all matched the recorded digest",
        },
    ]
    evidence["video"] = {
        mp4.name: {"sha256": sha256_of(mp4.read_bytes()), "bytes": mp4.stat().st_size}
        for mp4 in sorted((ctx.out / "mp4").glob("*.mp4"))
    }
    evidence_path = upload / "motion3d_canary_evidence.json"
    evidence_path.write_text(json.dumps(evidence, indent=2) + "\n")

    # Everything else (goldens, motion dump, per-clip sidecars) as one archive.
    archive = upload / "motion3d_canary_artifacts.tar.gz"
    with tarfile.open(archive, "w:gz") as handle:
        for item in sorted(ctx.out.rglob("*")):
            if upload == item or upload in item.parents or not item.is_file():
                continue
            handle.add(item, arcname=str(Path("canary3d") / item.relative_to(ctx.out)))

    manifest = {
        "what": "Chronon3d Motion3D canary — goldens, determinism, motion dump, same-motion, benchmark",
        "surface": "raw pre-encode RGBA8 1920x1080 via --video-sink raw (the PNG/still lane does not compose text)",
        "backend": "software (CPU)",
        "clips": CANARY_CLIPS,
        "golden_frames": GOLDEN_FRAMES,
        "files": [
            {
                "name": path.name,
                "bytes": path.stat().st_size,
                "sha256": sha256_of(path.read_bytes()),
            }
            for path in sorted(upload.iterdir())
            if path.is_file()
        ],
    }
    (upload / "motion3d_canary_manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"packaged {len(manifest['files'])} files in {upload}")
    ctx.report["package"] = manifest


def cmd_upload(ctx: Context, uploader: Path, credentials: Path, token: Path, folder: str) -> None:
    """Push the package to Drive with the repo's content-addressed uploader."""
    upload = ctx.out / "upload"
    files = sorted(path for path in upload.iterdir() if path.is_file())
    if not files:
        raise SystemExit("nothing to upload: run `package` first")
    for path in (uploader, credentials, token):
        if not path.exists():
            raise SystemExit(f"missing uploader input: {path}")

    record = {
        "what": "Chronon3d Motion3D canary published to Google Drive",
        "source": str(upload.relative_to(REPO_ROOT.parent) if upload.is_relative_to(REPO_ROOT.parent) else upload),
        "parent_folder_id": folder,
        "parent_folder_url": f"https://drive.google.com/drive/folders/{folder}",
        "uploader": str(uploader),
        "recorded_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "publications": [],
    }
    for path in files:
        digest = sha256_of(path.read_bytes())
        result = subprocess.run(
            [str(uploader), "-credentials", str(credentials), "-token", str(token),
             "-folder", folder, "-file", str(path), "-name", path.name, "-sha256", digest],
            cwd=str(REPO_ROOT.parent), capture_output=True, text=True)
        entry = {
            "name": path.name,
            "local_sha256": digest,
            "local_bytes": path.stat().st_size,
            "returncode": result.returncode,
            "stdout_tail": result.stdout.strip().splitlines()[-1] if result.stdout.strip() else "",
        }
        for match in re.finditer(r"\bid=([A-Za-z0-9_-]{10,})", result.stdout):
            entry.setdefault("drive_file_ids", []).append(match.group(1))
        for match in re.finditer(r"\blink=(https://drive\.google\.com/[^\s\"]+)", result.stdout):
            entry.setdefault("drive_links", []).append(match.group(1))
        record["publications"].append(entry)
        status = "PASS" if result.returncode == 0 else "FAIL"
        print(f"[{status}] {path.name} {digest[:12]} {entry['stdout_tail']}", flush=True)
        if result.returncode != 0:
            print(result.stderr[-2000:], file=sys.stderr)
    record["uploaded"] = sum(1 for entry in record["publications"] if entry["returncode"] == 0)
    record["failed"] = len(record["publications"]) - record["uploaded"]
    (ctx.out / "drive-publication.json").write_text(json.dumps(record, indent=2) + "\n")
    ctx.report["upload"] = {
        "parent_folder_id": folder,
        "uploaded": record["uploaded"],
        "failed": record["failed"],
        "files": [entry["name"] for entry in record["publications"]],
    }


def cmd_verify_publication(ctx: Context) -> None:
    """Check the publication record against the files actually on disk.

    The uploader is content-addressed: it is handed the local digest and echoes
    the digest and byte count it accepted.  That is a check the remote side
    agrees on; this step re-derives the digest locally so the record cannot
    drift from the artifacts, and reports the Drive file id and link per file.
    """
    record_path = ctx.out / "drive-publication.json"
    if not record_path.exists():
        raise SystemExit("nothing to verify: run `upload` first")
    record = load_json(record_path)
    manifest = load_json(ctx.out / "upload" / "motion3d_canary_manifest.json")
    manifest_digests = {entry["name"]: entry["sha256"] for entry in manifest["files"]}

    checked = 0
    mismatches = []
    for entry in record["publications"]:
        path = ctx.out / "upload" / entry["name"]
        if not path.exists():
            mismatches.append({"name": entry["name"], "why": "missing locally"})
            continue
        digest = sha256_of(path.read_bytes())
        echoed = re.search(r"sha256=([0-9a-f]{64})", entry.get("stdout_tail", ""))
        # The manifest cannot carry its own digest, so for that one file the
        # comparison is against the digest recorded at upload time.
        in_manifest = manifest_digests.get(entry["name"], digest)
        ok = (digest == entry["local_sha256"] == in_manifest
              and entry["returncode"] == 0
              and (echoed is None or echoed.group(1) == digest))
        entry["verified_sha256"] = digest
        entry["verified"] = ok
        # The uploader prints the id and the link on the line we kept, so the
        # publication record can carry them without re-uploading the file.
        if not entry.get("drive_file_ids"):
            found = re.search(r"\bid=([A-Za-z0-9_-]{10,})", entry.get("stdout_tail", ""))
            if found:
                entry["drive_file_ids"] = [found.group(1)]
        if not entry.get("drive_links"):
            found = re.search(r"\blink=(https://drive\.google\.com/[^\s\"]+)", entry.get("stdout_tail", ""))
            if found:
                entry["drive_links"] = [found.group(1)]
        if not ok:
            mismatches.append({"name": entry["name"], "why": "digest disagreement"})
        checked += 1

    record["verification"] = {
        "method": "re-hash each shipped file locally and compare it with the manifest, "
                  "the digest recorded at upload time and the digest the uploader echoed",
        "files_checked": checked,
        "mismatches": mismatches,
        "all_verified": not mismatches and checked == len(record["publications"]),
        "note": "the digest proves the bytes are the ones that were packaged; it does not "
                "re-download them from Drive, which the uploader has no read mode for",
    }
    record_path.write_text(json.dumps(record, indent=2) + "\n")
    ctx.report["publication"] = record["verification"]
    print(f"verified {checked} file(s), mismatches: {mismatches}")


def cmd_report(ctx: Context) -> None:
    """Render the human-readable summary of everything measured."""
    report_path = ctx.out / "motion3d_canary_report.md"
    lines: list[str] = []
    add = lines.append

    add("# Motion3D canary report")
    add("")
    add(f"- generated: {time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())}")
    add(f"- host: {os.uname().machine}, {os.cpu_count()} logical cores")
    add(f"- binary: `{ctx.cli.relative_to(REPO_ROOT)}`")
    add("- backend: software (CPU), 1920x1080, 30 fps, 90 frames per clip")
    add("- pixel surface: raw pre-encode RGBA8 (`--video-sink raw`) — the CLI's "
        "PNG/still lane explicitly does not compose text layers")
    add("")

    add("## The clips")
    add("")
    add("| clip | what it puts under load |")
    add("|---|---|")
    for clip, note in [
        ("Motion3DGlowOrbit", "real text + real image + parent null + 3D camera orbit + glow"),
        ("Motion3DSentenceOrbit", "a two-line sentence: metrics, bounds, wrap, clipping, glow past the box"),
        ("Motion3DTextParentCamera", "text → Null_B → Null_A with the camera under Null_A: World = ParentWorld × Local"),
        ("Motion3DSameMotionText / …Image", "the same authored transform on a text layer and on an image layer"),
    ]:
        add(f"| `{clip}` | {note} |")
    add("")

    add("## Verdict")
    add("")
    add("| check | evidence | result |")
    add("|---|---|---|")
    goldens_for_verdict = ctx.report.get("goldens")
    if goldens_for_verdict:
        distinct = all(len(set(hashes.values())) == len(hashes) for hashes in goldens_for_verdict.values())
        add(f"| goldens: {len(goldens_for_verdict)} clips × {len(GOLDEN_FRAMES)} frames, all frames distinct "
            f"| raw pre-encode hashes | **{'PASS' if distinct else 'FAIL'}** |")
    det = ctx.report.get("determinism")
    if det:
        add(f"| random access: frames `{OUT_OF_ORDER}` bit-identical to the sequential pass "
            f"| {len(det['out_of_order']['results'])} re-renders | "
            f"**{'PASS' if det['out_of_order']['all_bit_identical'] else 'FAIL'}** |")
        add(f"| repeated frame {det['repeated_frame']['frame']} × {det['repeated_frame']['runs']} "
            f"| {len(det['repeated_frame']['distinct'])} distinct digest(s) | "
            f"**{'PASS' if det['repeated_frame']['all_bit_identical'] else 'FAIL'}** |")
    same_for_verdict = ctx.report.get("same_motion")
    if same_for_verdict:
        add(f"| same transform on a text layer and an image layer sweeps the same trajectory "
            f"| {same_for_verdict['frames_compared']} frames, residual "
            f"{same_for_verdict['motion_delta_len_px']} px vs {same_for_verdict['inset_bound_px']} px bound "
            f"| **{'PASS' if same_for_verdict['passes'] else 'FAIL'}** |")
    recheck = ctx.report.get("recheck")
    if recheck and "all_match" in recheck:
        hero_entry = recheck.get(HERO_CLIP, {})
        frames_checked = 90 if "all_90_frames_checked" in hero_entry else len(GOLDEN_FRAMES)
        add(f"| re-render on a freshly built binary: {frames_checked} hero frames + "
            f"{len(ctx.clips) * len(GOLDEN_FRAMES)} golden hashes re-derived "
            f"| bytes | **{'PASS' if recheck['all_match'] else 'FAIL'}** |")
    bench_for_verdict = ctx.report.get("benchmark")
    if bench_for_verdict:
        add(f"| {len(bench_for_verdict)} clips render to 1920x1080 30 fps h264 | "
            f"`mp4/` | **PASS** |")
    add("")

    goldens = ctx.report.get("goldens")
    if goldens:
        add("## Goldens (raw pre-encode hashes)")
        add("")
        add("| clip | " + " | ".join(f"f{f}" for f in GOLDEN_FRAMES) + " |")
        add("|---" * (len(GOLDEN_FRAMES) + 1) + "|")
        for clip, hashes in goldens.items():
            add(f"| `{clip}` | " + " | ".join(hashes[str(f)][:10] for f in GOLDEN_FRAMES) + " |")
        add("")
        add("Full hashes: `goldens/<clip>/hashes.json`; PNGs next to them for human inspection.")
        add("")

    determinism = ctx.report.get("determinism")
    if determinism:
        ooo = determinism["out_of_order"]
        rep = determinism["repeated_frame"]
        add("## Determinism (random access, on the hero clip)")
        add("")
        add(f"- frames asked for out of order `{OUT_OF_ORDER}`: "
            f"**{'PASS' if ooo['all_bit_identical'] else 'FAIL'}** "
            f"({sum(1 for r in ooo['results'] if r['bit_identical'])}/{len(ooo['results'])} bit-identical to the sequential pass)")
        add(f"- frame {rep['frame']} rendered {rep['runs']} times: "
            f"**{'PASS' if rep['all_bit_identical'] else 'FAIL'}** "
            f"({len(rep['distinct'])} distinct digest(s))")
        add("")
        add("| request # | frame | sha256 (first 10) | == sequential |")
        add("|---|---|---|---|")
        for row in ooo["results"]:
            add(f"| {row['request_position']} | {row['frame']} | {row['sha256'][:10]} | {'yes' if row['bit_identical'] else 'NO'} |")
        add("")

    dump = ctx.report.get("motion_dump")
    if dump:
        add("## Motion dump")
        add("")
        add("`motion_dump/motion_dump.json` holds, per golden frame, two independent "
            "records: the engine's graph preflight report — every node's "
            "**projected bbox in canvas space**, emitted by the production CLI via "
            "`--diagnostic-plan --diagnostic-plan-output` — and the ink bbox "
            "measured from the raw pre-encode frame. Read together they localise a "
            "failure: a wrong bbox trajectory is a motion problem, a right bbox "
            "with wrong pixels is a raster/composite problem.")
        add("")
        add("The per-frame headers/tables are kept next to it as "
            "`motion_dump/<clip>/preflight_<frame>.txt`.")
        add("")

    same = ctx.report.get("same_motion")
    if same:
        add("## Same motion: text layer vs image layer")
        add("")
        add(f"- frames compared: {same['frames_compared']}")
        add(f"- text-layer ink displacement: {same['text_motion_px']} px")
        add(f"- image-layer ink displacement: {same['image_motion_px']} px")
        add(f"- difference {same['motion_delta_px']} px ({same['motion_delta_len_px']} px), "
            f"bound {same['inset_bound_px']} px (a quarter of the "
            f"{same['authored_box_diagonal_px']} px box diagonal): "
            f"**{'PASS' if same['passes'] else 'FAIL'}**")
        add("")
        add(f"  {same['what_this_cannot_prove']}")
        add("")
        add("The engine's own node table for both clips at each golden frame is "
            "in `same_motion/same_motion.json` (`engine_node_rows`), for reading "
            "next to the numbers above.")
        add("")

    bench = ctx.report.get("benchmark")
    if bench:
        add("## Benchmark (software backend, 90 frames, 1920x1080)")
        add("")
        add("| clip | lane | wall s | render ms/frame | prepare ms | encode ms | peak RSS MB | fps |")
        add("|---|---|---|---|---|---|---|---|")
        for clip, entry in bench.items():
            for lane in ("raw", "video"):
                data = entry[lane]
                phases = data.get("phases_engine") or {}
                encode = phases.get("encode_drain_ms")
                fps = data.get("end_to_end_fps")
                add("| `{}` | {} | {} | {} | {} | {} | {} | {} |".format(
                    clip,
                    data.get("lane"),
                    round((data.get("wall_ms_engine") or 0) / 1000.0, 2),
                    round(data.get("avg_frame_ms") or 0.0, 1),
                    round(phases.get("prepare_ms") or 0.0, 1),
                    "n/a" if encode is None else round(encode, 1),
                    data.get("peak_rss_mb"),
                    "n/a" if fps is None else round(fps, 2),
                ))
        add("")
        add("Per-clip detail (startup, prepare, graph, encoder) lives in "
            "`benchmark/benchmark.json` and the engine's own `*.timing.json` sidecars.")
        add("")

    add("## Re-check on a rebuilt binary")
    add("")
    if not recheck:
        add("Not run.")
    else:
        for clip, entry in recheck.items():
            if not isinstance(entry, dict):
                continue
            gate = entry.get("tool_gate") or {}
            add(f"- `{clip}`: {entry.get('golden_frames_checked')} golden frames re-derived, "
                f"mismatches {entry.get('mismatches')}"
                + (f", all 90 frames checked, mismatches {entry.get('all_90_frames_mismatches')}"
                   if "all_90_frames_checked" in entry else "")
                + f"; `tools/raw_frame_hash.py --golden` "
                + ("**PASS**" if gate.get("passes") else f"**FAIL** ({gate.get('detail', '')[:200]})"))
        add("")
        shas = {entry.get("tool_gate", {}).get("golden_engine_sha"):
                entry.get("tool_gate", {}).get("current_engine_sha")
                for entry in recheck.values() if isinstance(entry, dict)}
        for recorded, current in shas.items():
            if recorded:
                add(f"- provenance: the golden manifests record `engine_sha={recorded}`, "
                    f"the binary that re-derived them reports `engine_sha={current}`. "
                    "The gate compares frame lines; the sha line is provenance, so a "
                    "moved HEAD is reported here rather than counted as a pixel change.")
        add("")
        add("The archive in the upload set contains `recheck/rebuild_recheck.json` with "
            "the full digest lists.")
    add("")

    add("## Observed once, not reproduced")
    add("")
    add("During this session one re-check pass reported the hero clip's **frame 22** "
        "differing from the sequential baseline (1 frame of 90, one pass of several). "
        "It was not reproducible: the same frame rendered 12 times, three further "
        "full 0-89 passes and the final re-check all produced the recorded digest, "
        "and every other frame matched in every pass.  The raw bytes of the "
        "anomalous frame were not kept, so the digest is all the evidence there is. "
        "It is recorded here as an open observation about single-frame "
        "reproducibility under load, not as a passing result and not as a proven "
        "defect.")
    add("")

    add("## What this does NOT claim")
    add("")
    add("- The text is a **2.5D plane**, not extruded geometry.  Orbiting to 90° "
        "turns a word into a line; that is correct for a plane, and no depth "
        "number is fabricated to make the demo look volumetric.")
    add("- The software backend is the lane measured here.  A GPU/NVENC lane is a "
        "separate measurement.")
    add("- The dump is **projected bounding boxes**, not the raw 4x4 matrices: the "
        "CLI exposes the resolved geometry per node, not a matrix dump.  That is "
        "enough to tell a motion error from a raster error, and it is the strongest "
        "geometry record this build produces.")
    add("- Only the golden frames are dumped; a full per-frame dump would be 90 "
        "preflight reports per clip.")
    add("")

    report_path.write_text("\n".join(lines) + "\n")
    print(f"report: {report_path}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=["all", "goldens", "determinism", "motion-dump",
                                            "same-motion", "benchmark", "recheck",
                                            "package", "report", "upload", "verify-publication"])
    parser.add_argument("--uploader", type=Path,
                        default=REPO_ROOT.parent / "RenderingGen/bin/drive-upload")
    parser.add_argument("--credentials", type=Path,
                        default=REPO_ROOT.parent / "RenderingGen/infra/docker/credentials.json")
    parser.add_argument("--token", type=Path,
                        default=REPO_ROOT.parent / "RenderingGen/infra/docker/token.json")
    parser.add_argument("--drive-folder", default="1X0nyiF82tMihNfRTAlij__bv7LwzZJ75")
    parser.add_argument("--cli", type=Path, default=DEFAULT_CLI)
    parser.add_argument("--out", type=Path, default=REPO_ROOT / "output/canary3d")
    parser.add_argument("--work", type=Path, default=None,
                        help="scratch dir for the big raw passes (default: <out>/.scratch)")
    parser.add_argument("--clip", action="append", default=None,
                        help="restrict to one clip (repeatable)")
    args = parser.parse_args()

    cli = args.cli if args.cli.is_absolute() else REPO_ROOT / args.cli
    # `report`, `package` and `upload` only read what earlier steps already
    # wrote, so they must work on a host where the binary is not built.
    if args.command not in ("report", "package", "upload") and not cli.exists():
        raise SystemExit(f"CLI not found: {cli}")

    out = args.out if args.out.is_absolute() else REPO_ROOT / args.out
    work = args.work or (out / ".scratch")
    work.mkdir(parents=True, exist_ok=True)
    out.mkdir(parents=True, exist_ok=True)

    ctx = Context(cli=cli, out=out, work=work, clips=args.clip or CANARY_CLIPS)

    report_path = out / "report_state.json"
    if report_path.exists() and args.command != "all":
        try:
            ctx.report = json.loads(report_path.read_text())
        except Exception:
            ctx.report = {}

    steps = {
        "goldens": cmd_goldens,
        "determinism": cmd_determinism,
        "motion-dump": cmd_motion_dump,
        "same-motion": cmd_same_motion,
        "benchmark": cmd_benchmark,
    }

    order = ["goldens", "determinism", "motion-dump", "same-motion", "benchmark"]
    try:
        if args.command == "all":
            for step in order:
                print(f"== {step}", flush=True)
                steps[step](ctx)
            print("== package", flush=True)
            cmd_package(ctx)
        elif args.command == "recheck":
            cmd_recheck(ctx)
        elif args.command == "package":
            cmd_package(ctx)
        elif args.command == "upload":
            cmd_upload(ctx, args.uploader, args.credentials, args.token, args.drive_folder)
        elif args.command == "verify-publication":
            cmd_verify_publication(ctx)
        elif args.command == "report":
            cmd_report(ctx)
            return 0
        else:
            steps[args.command](ctx)
    finally:
        ctx.cleanup()

    report_path.write_text(json.dumps(ctx.report, indent=2) + "\n")
    cmd_report(ctx)
    return 0


if __name__ == "__main__":
    sys.exit(main())
