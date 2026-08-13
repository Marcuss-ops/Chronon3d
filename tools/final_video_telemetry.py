#!/usr/bin/env python3
"""Normalize Chronon's JSONL run record plus ffprobe/time data into a sidecar."""
import argparse
import json
import pathlib
import re
import sqlite3
import subprocess


def probe(path):
    raw = subprocess.check_output([
        "ffprobe", "-v", "error", "-count_frames", "-select_streams", "v:0",
        "-show_entries", "stream=width,height,r_frame_rate,avg_frame_rate,nb_read_frames,codec_name,pix_fmt",
        "-of", "json", str(path)], text=True)
    stream = json.loads(raw).get("streams", [{}])[0]
    rate = stream.get("avg_frame_rate") or stream.get("r_frame_rate", "0/1")
    n, d = (rate.split("/", 1) + ["1"])[:2]
    stream["fps"] = float(n) / float(d) if float(d) else 0.0
    return stream


def time_value(path, label):
    text = pathlib.Path(path).read_text(errors="replace")
    match = re.search(rf"^{re.escape(label)}:\s*(\d+)", text, re.MULTILINE)
    return int(match.group(1)) * 1024 if match else 0


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--manifest", required=True)
    p.add_argument("--case-id", required=True)
    p.add_argument("--mp4", required=True)
    p.add_argument("--telemetry-dir", required=True)
    p.add_argument("--time-log", required=True)
    p.add_argument("--log", required=True)
    p.add_argument("--output", required=True)
    a = p.parse_args()
    manifest = json.load(open(a.manifest))
    case = next(c for c in manifest["cases"] if c["id"] == a.case_id)
    mp4 = pathlib.Path(a.mp4).resolve()
    record = {}
    history = pathlib.Path(a.telemetry_dir) / "render_history.jsonl"
    if history.exists():
        for line in history.read_text(errors="replace").splitlines():
            try:
                item = json.loads(line)
            except json.JSONDecodeError:
                continue
            if item.get("output_path") == str(mp4):
                record = item
    if not record:
        # SQLite is the canonical telemetry store when the CLI is built with
        # CHRONON3D_ENABLE_SQLITE_TELEMETRY. Keep the sidecar usable when the
        # optional JSONL mirror is disabled or not emitted by that build.
        for db in sorted(pathlib.Path(a.telemetry_dir).glob("*.sqlite")):
            with sqlite3.connect(db) as connection:
                connection.row_factory = sqlite3.Row
                candidates = (str(mp4), str(mp4.with_name(mp4.name[:-4] + ".partial.mp4")))
                row = connection.execute(
                    "SELECT * FROM render_runs WHERE output_path IN (?, ?) "
                    "ORDER BY rowid DESC LIMIT 1", candidates
                ).fetchone()
                if row is not None:
                    record = dict(row)
                    break
    stream = probe(mp4)
    expected = int(case["frames"])
    telemetry = {
        "schema": "chronon.final_video_telemetry.v1",
        "case_id": case["id"],
        "case_name": case["name"],
        "composition": case["composition"],
        "tags": case["tags"],
        "frames": expected,
        "frames_written": record.get("frames_written", 0),
        "wall_ms": record.get("wall_time_ms", 0),
        "render_ms": record.get("render_ms", 0),
        "encode_ms": record.get("encode_ms", 0),
        "peak_rss_bytes": time_value(a.time_log, "Maximum resident set size"),
        "arena_peak_bytes": record.get("arena_peak_bytes", 0),
        "logical_resource_bytes": record.get("logical_resource_bytes", 0),
        "physical_resource_bytes": record.get("physical_resource_bytes", 0),
        "alias_saved_bytes": record.get("alias_saved_bytes", 0),
        "cache_hits": record.get("cache_hits", 0),
        "cache_misses": record.get("cache_misses", 0),
        "nodes_executed": record.get("nodes_executed", 0),
        "nodes_skipped": record.get("nodes_skipped", 0),
        "tiles_total": record.get("tiles_total", 0),
        "tiles_skipped": record.get("tiles_skipped", 0),
        "heap_allocations_hot_loop": record.get("heap_allocations_hot_loop", 0),
        "roi_pixels": record.get("roi_pixels", 0),
        "blur_source_pixels": record.get("blur_pixels", 0),
        "scratch_bytes": record.get("scratch_bytes", 0),
        "max_radius": record.get("max_radius", 0),
        "output": {
            "path": str(mp4), "width": stream.get("width", 0),
            "height": stream.get("height", 0), "fps": stream.get("fps", 0),
            "codec": stream.get("codec_name", ""), "pix_fmt": stream.get("pix_fmt", ""),
            "decoded_frames": int(stream.get("nb_read_frames", 0) or 0)
        },
        "source_run": record,
    }
    required_metrics = [
        "arena_peak_bytes", "logical_resource_bytes", "physical_resource_bytes",
        "alias_saved_bytes", "nodes_skipped", "tiles_skipped", "scratch_bytes",
    ]
    telemetry["unpopulated_metrics"] = [
        key for key in required_metrics if key not in record
    ]
    pathlib.Path(a.output).write_text(json.dumps(telemetry, indent=2, sort_keys=True) + "\n")


if __name__ == "__main__":
    main()
