#!/usr/bin/env python3
"""Prune old telemetry runs from the Chronon3D SQLite database.

Deletes all runs except the N most recent ones, then VACUUMs the database
to reclaim disk space. Supports --dry-run to preview what would be deleted.

Usage:
    python3 tools/telemetry_dashboard/prune_telemetry.py                   # keep 50 (default)
    python3 tools/telemetry_dashboard/prune_telemetry.py --keep 20         # keep 20 most recent
    python3 tools/telemetry_dashboard/prune_telemetry.py --dry-run         # preview only
    python3 tools/telemetry_dashboard/prune_telemetry.py --db /path/to.db  # custom DB path

Exit codes: 0 = success, 1 = error.
"""

import argparse
import logging
import os
import sqlite3
import sys
import shutil
from datetime import datetime, timezone

TELEMETRY_DIR = os.path.expanduser("~/.chronon3d/telemetry")
DEFAULT_DB = os.path.join(TELEMETRY_DIR, "chronon3d_render_history.sqlite")

TABLES = [
    "render_runs",
    "render_frames",
    "render_phase_events",
    "render_counters",
    "render_node_events",
    "render_layer_events",
    "render_cache_events",
    "render_culling_events",
    "render_text_events",
    "render_image_events",
    "render_tile_events",
]

log = logging.getLogger("prune_telemetry")


def get_row_counts(cursor: sqlite3.Cursor, tables: list[str]) -> dict[str, int]:
    """Return row counts for all tables."""
    counts = {}
    for t in tables:
        try:
            cursor.execute(f"SELECT COUNT(*) FROM {t}")
            counts[t] = cursor.fetchone()[0]
        except sqlite3.OperationalError:
            counts[t] = 0
    return counts


def prune(db_path: str, keep: int, dry_run: bool) -> None:
    """Prune the telemetry database, keeping only the `keep` most recent runs."""
    if not os.path.exists(db_path):
        log.error("Database not found: %s", db_path)
        sys.exit(1)

    db_size_before = os.path.getsize(db_path)
    conn = sqlite3.connect(db_path)
    c = conn.cursor()

    # Get total runs
    c.execute("SELECT COUNT(*) FROM render_runs")
    total = c.fetchone()[0]
    log.info("Total runs in DB: %d", total)

    if total <= keep:
        log.info("Nothing to prune (%d <= %d). Exiting.", total, keep)
        conn.close()
        return

    to_delete = total - keep
    log.info("Will delete %d runs (keeping %d most recent)", to_delete, keep)

    # Get run_ids to keep
    c.execute(
        "SELECT run_id, started_at_iso FROM render_runs ORDER BY started_at_iso DESC LIMIT ?",
        (keep,),
    )
    keep_rows = c.fetchall()
    keep_ids = [r[0] for r in keep_rows]

    if not keep_ids:
        log.error("No runs found to keep.")
        conn.close()
        sys.exit(1)

    log.info(
        "Keeping runs from %s to %s",
        keep_rows[-1][1] if keep_rows else "?",
        keep_rows[0][1] if keep_rows else "?",
    )

    # Count rows before
    before = get_row_counts(c, TABLES)

    if dry_run:
        log.info("=== DRY RUN — no changes will be made ===")
        # Show what would be deleted
        placeholders = ",".join(["?"] * len(keep_ids))
        for t in TABLES:
            c.execute(
                f"SELECT COUNT(*) FROM {t} WHERE run_id NOT IN ({placeholders})",
                keep_ids,
            )
            would_delete = c.fetchone()[0]
            if would_delete > 0:
                log.info("  Would delete %d rows from %s", would_delete, t)
        conn.close()
        return

    # Clean up previous backups before creating a new one
    for suffix in [".pre-prune-backup", ".pre-cleanup-backup"]:
        old = db_path + suffix
        if os.path.exists(old):
            os.remove(old)
            log.info("Removed previous backup: %s", old)

    # Create backup before pruning
    backup_path = db_path + ".pre-prune-backup"
    try:
        shutil.copy2(db_path, backup_path)
        log.info("Backup created: %s", backup_path)
    except OSError as e:
        log.warning("Could not create backup: %s", e)

    # Delete old rows
    placeholders = ",".join(["?"] * len(keep_ids))
    total_deleted = 0
    for t in TABLES:
        c.execute(f"DELETE FROM {t} WHERE run_id NOT IN ({placeholders})", keep_ids)
        deleted = c.rowcount
        total_deleted += deleted
        if deleted > 0:
            log.info("  Deleted %d rows from %s (was %d)", deleted, t, before[t])

    conn.commit()

    # Count rows after
    after = get_row_counts(c, TABLES)
    conn.close()

    # VACUUM to reclaim space
    log.info("Running VACUUM...")
    vacuum_conn = sqlite3.connect(db_path)
    vacuum_conn.execute("VACUUM")
    vacuum_conn.close()

    db_size_after = os.path.getsize(db_path)
    freed_mb = (db_size_before - db_size_after) / (1024 * 1024)

    log.info("=== PRUNE COMPLETE ===")
    log.info("  Runs: %d → %d (deleted %d)", total, after["render_runs"], to_delete)
    log.info("  Total rows deleted: %d", total_deleted)
    log.info("  DB size: %.1f MB → %.1f MB (freed %.1f MB)",
             db_size_before / (1024 * 1024),
             db_size_after / (1024 * 1024),
             freed_mb)




def main() -> None:
    parser = argparse.ArgumentParser(
        description="Prune old Chronon3D telemetry runs from the SQLite database."
    )
    parser.add_argument(
        "--db",
        default=DEFAULT_DB,
        help=f"Path to SQLite database (default: {DEFAULT_DB})",
    )
    parser.add_argument(
        "--keep",
        type=int,
        default=50,
        help="Number of most recent runs to keep (default: 50)",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Preview what would be deleted without making changes",
    )
    parser.add_argument(
        "--verbose", "-v",
        action="store_true",
        help="Enable debug logging",
    )
    args = parser.parse_args()

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s [%(levelname)s] %(message)s",
        datefmt="%Y-%m-%d %H:%M:%S",
    )

    if args.dry_run:
        log.info("DRY RUN MODE — no changes will be made")

    prune(args.db, args.keep, args.dry_run)


if __name__ == "__main__":
    main()
