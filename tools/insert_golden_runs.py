#!/usr/bin/env python3
"""Insert golden reference images into ALL telemetry databases as runs."""
import sqlite3
from pathlib import Path
from datetime import datetime, timezone

PROJECT_ROOT = Path(__file__).resolve().parent.parent
GOLDEN_DIR = PROJECT_ROOT / "test_renders" / "golden"

# Both databases that the servers read from:
DB_PATHS = [
    PROJECT_ROOT / "output" / "telemetry.db",                           # used by dashboard_server.py (port 8080)
    Path.home() / ".chronon3d" / "telemetry" / "chronon3d_render_history.sqlite",  # used by flask_app.py (port 8000)
]

if not GOLDEN_DIR.exists():
    print(f"ERROR: Golden directory not found at {GOLDEN_DIR}")
    exit(1)

golden_files = sorted(GOLDEN_DIR.rglob("*.png"), key=lambda p: p.name)
print(f"Found {len(golden_files)} golden images")

now_iso = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
git_commit = "golden_ref"

for db_path in DB_PATHS:
    if not db_path.exists():
        print(f"SKIP {db_path} (not found)")
        continue

    print(f"\n=== Processing: {db_path} ===")
    conn = sqlite3.connect(str(db_path))
    cursor = conn.cursor()

    # Ensure run_id column exists (use the same schema)
    cursor.execute("""
        CREATE TABLE IF NOT EXISTS render_runs (
            run_id TEXT PRIMARY KEY,
            composition_id TEXT,
            output_path TEXT,
            success INTEGER,
            error_code TEXT,
            error_message TEXT,
            frames_total INTEGER,
            frames_written INTEGER,
            wall_time_ms REAL,
            render_ms REAL,
            encode_ms REAL,
            effective_fps REAL,
            pixels_touched INTEGER,
            cache_hits INTEGER,
            cache_misses INTEGER,
            nodes_executed INTEGER,
            layers_rendered INTEGER,
            text_glyphs_rasterized INTEGER,
            images_sampled INTEGER,
            blur_pixels INTEGER,
            simd_lerp_calls INTEGER,
            tiles_total INTEGER,
            tiles_hit INTEGER,
            tiles_miss INTEGER,
            tiles_partial INTEGER,
            bytes_allocated_peak INTEGER,
            started_at_iso TEXT,
            finished_at_iso TEXT,
            git_commit_short TEXT,
            build_type TEXT,
            compiler_info TEXT,
            os TEXT,
            cpu_model TEXT,
            cores INTEGER
        )
    """)

    inserted = 0
    skipped = 0

    for f in golden_files:
        rel = str(f.relative_to(PROJECT_ROOT))
        comp_name = f.stem  # e.g. "image_proofs_golden"
        run_id = f"golden_{comp_name}"

        cursor.execute("SELECT COUNT(*) FROM render_runs WHERE run_id = ?", (run_id,))
        if cursor.fetchone()[0] > 0:
            cursor.execute(
                "UPDATE render_runs SET output_path = ?, finished_at_iso = ? WHERE run_id = ?",
                (rel, now_iso, run_id)
            )
            skipped += 1
        else:
            cursor.execute(
                """INSERT INTO render_runs (
                    run_id, composition_id, output_path, success, frames_total, frames_written,
                    wall_time_ms, render_ms, encode_ms, effective_fps,
                    pixels_touched, cache_hits, cache_misses, nodes_executed, layers_rendered,
                    frames_total, frames_written,
                    started_at_iso, finished_at_iso, git_commit_short, build_type,
                    compiler_info, os, cpu_model, cores
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)""",
                (
                    run_id, comp_name, rel, 1, 1, 1,
                    1.0, 1.0, 0.0, 1.0,
                    1920 * 1080, 0, 1, 1, 1,
                    1, 1,
                    now_iso, now_iso, git_commit, "Release",
                    "Golden Test", "Linux", "Generic CPU", 8
                )
            )
            inserted += 1
            print(f"  {run_id} -> {rel}")

    conn.commit()
    conn.close()
    print(f"  => {inserted} inserted, {skipped} updated")

print("\nDone!")
