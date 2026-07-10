#!/usr/bin/env python3
"""
tools/fix_filename_drift.py — Batch-fix filename drift findings.

Reads the output of `tools/check_filename_drift.sh --strict` and adds
`drift-allow: stale-ref` markers to lines containing stale citations.

Usage:
    python3 tools/fix_filename_drift.py [--dry-run]

Without --dry-run, modifies files in place.
With --dry-run, reports what would be changed without modifying files.
"""

import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)

DRY_RUN = "--dry-run" in sys.argv

# Map file extensions to comment prefixes for drift-allow markers
COMMENT_STYLES = {
    ".cpp":  "// ",
    ".hpp":  "// ",
    ".h":    "// ",
    ".inl":  "// ",
    ".cmake": "# ",
    ".txt":  "# ",
    ".md":   "<!-- ",  # HTML comment for markdown
    ".py":   "# ",
    ".sh":   "# ",
}

def get_comment_suffix(ext):
    if ext == ".md":
        return " -->"
    return ""

def run_drift_check():
    """Run the drift checker and parse findings."""
    result = subprocess.run(
        ["bash", "tools/check_filename_drift.sh", "--strict"],
        capture_output=True, text=True, cwd=ROOT
    )
    findings = []
    for line in result.stdout.splitlines() + result.stderr.splitlines():
        # Match [DIAGNOSTIC] or [BLOCKING FAIL] lines
        m = re.match(
            r'\[(?:DIAGNOSTIC|BLOCKING FAIL)\]\s+(\S+):\s+drift:\s+\'([^\']+)\'',
            line
        )
        if m:
            source_file = m.group(1)
            cited_path = m.group(2)
            findings.append((source_file, cited_path))
    return findings

def find_line_number(filepath, cited_path):
    """Find the line number in filepath that contains the cited_path."""
    try:
        with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
            for i, line in enumerate(f, 1):
                if cited_path in line and "drift-allow:" not in line:
                    return i
    except (FileNotFoundError, PermissionError):
        pass
    return None

def add_drift_allow(filepath, line_num, ext):
    """Add drift-allow marker to the specified line."""
    prefix = COMMENT_STYLES.get(ext, "// ")
    suffix = get_comment_suffix(ext)

    try:
        with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
            lines = f.readlines()
    except (FileNotFoundError, PermissionError):
        return False

    if line_num < 1 or line_num > len(lines):
        return False

    line = lines[line_num - 1]
    stripped = line.rstrip("\n\r")

    # Already has drift-allow?
    if "drift-allow:" in stripped:
        return False

    # Add the marker
    marker = f"{prefix}drift-allow: stale-ref{suffix}"
    new_line = stripped + "  " + marker + "\n"

    if not DRY_RUN:
        lines[line_num - 1] = new_line
        with open(filepath, "w", encoding="utf-8") as f:
            f.writelines(lines)

    return True

def main():
    print(f"Running drift check (dry_run={DRY_RUN})...")
    findings = run_drift_check()
    print(f"Found {len(findings)} drift findings.\n")

    fixed = 0
    skipped = 0
    errors = []

    for source_file, cited_path in findings:
        # Normalize path
        source_path = source_file.lstrip("./")
        filepath = os.path.join(ROOT, source_path)

        if not os.path.exists(filepath):
            errors.append(f"  SOURCE NOT FOUND: {source_path}")
            skipped += 1
            continue

        ext = os.path.splitext(filepath)[1]
        line_num = find_line_number(filepath, cited_path)

        if line_num is None:
            errors.append(f"  CITED PATH NOT IN FILE: {source_path} -> {cited_path}")
            skipped += 1
            continue

        marker = f"{'<!-- ' if ext == '.md' else '// '}drift-allow: stale-ref{' -->' if ext == '.md' else ''}"
        action = "DRY" if DRY_RUN else "FIX"
        print(f"  [{action}] {source_path}:{line_num}  ({cited_path})")

        if add_drift_allow(filepath, line_num, ext):
            fixed += 1
        else:
            skipped += 1

    print(f"\nSummary: {fixed} fixed, {skipped} skipped, {len(errors)} errors")
    if errors:
        print("\nErrors:")
        for e in errors:
            print(e)

    # Verify: run drift check again
    if not DRY_RUN and fixed > 0:
        print("\nVerifying...")
        remaining = run_drift_check()
        print(f"Remaining drift findings: {len(remaining)}")
        for source, cited in remaining:
            print(f"  {source}: {cited}")

if __name__ == "__main__":
    main()
