#!/usr/bin/env python3
"""source_census.py — negative census for tracked C++ sources and headers.

Answers one question per file: is it reachable from a build target, an
`#include`, a CMake list, or any other tracked consumer?

Reference sources (all optional, union of what exists on the host):

  1. CMake text      — every tracked `CMakeLists.txt`, `*.cmake`, `CMakePresets.json`.
  2. Configured lanes — every `build/chronon/*/compile_commands.json` and
     `.tmp/chronon-builds/*/compile_commands.json` (compdb `file` entries plus the
     `CMakeFiles/**/Unity/*.cxx` include lists that unity builds generate).
  3. Tracked text    — `git grep` of the basename for candidates only (manifests,
     scripts, docs, tests).

Verdicts:

  REFERENCED    at least one non-doc consumer (build graph or CMake text)
  DOC_ONLY      referenced exclusively from docs/markdown (may be stale prose)
  GLOB_COVERED  directory is covered by a file(GLOB...) source discovery
  ORPHAN        no reference found at all

Exit codes: 0 = report only (default), 1 = `--strict` and at least one ORPHAN.

Usage:
  python3 tools/source_census.py [--json out.json] [--strict] [--no-headers]
                                 [--root DIR]
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import re
import subprocess
import sys
from pathlib import Path

SOURCE_EXTS = {".cpp", ".cc", ".cxx", ".c"}
HEADER_EXTS = {".hpp", ".hh", ".hxx", ".h"}

EXCLUDED_DIR_PARTS = (
    "thirdparty", "vendor", "vcpkg_installed", "build", ".tmp", ".git",
    "node_modules", "out", "output",
)

DOC_PREFIXES = ("docs/",)
DOC_EXTS = {".md", ".txt", ".json", ".yaml", ".yml"}


def sh(args: list[str], cwd: Path) -> str:
    return subprocess.run(args, cwd=cwd, capture_output=True, text=True).stdout


def tracked_files(root: Path) -> list[str]:
    return [f for f in sh(["git", "ls-files"], root).splitlines() if f]


def is_universe_file(path: str, exts: set[str]) -> bool:
    if not any(path.endswith(e) for e in exts):
        return False
    parts = path.split("/")
    if any(p in EXCLUDED_DIR_PARTS for p in parts):
        return False
    if path.startswith(("src/", "apps/", "tests/", "include/", "bindings/", "tools/")):
        return True
    return False


def cmake_text_refs(root: Path, tracked: list[str]) -> tuple[set[str], str]:
    """Return the concatenated CMake text and a set of referenced basenames."""
    text = []
    for f in tracked:
        if f.endswith("CMakeLists.txt") or f.endswith(".cmake") or f == "CMakePresets.json":
            try:
                text.append((root / f).read_text(encoding="utf-8", errors="replace"))
            except OSError:
                pass
    blob = "\n".join(text)
    names = set(re.findall(r"[A-Za-z0-9_./+-]+", blob))
    return names, blob


def glob_covered_dirs(cmake_blob: str, root: Path) -> list[re.Pattern[str]]:
    """Directories discovered through file(GLOB...) source lists."""
    pats: list[re.Pattern[str]] = []
    for m in re.finditer(r"file\s*\(\s*GLOB(?:_RECURSE)?\s+[^)]*?[\"']?([^\"'\s)]+)", cmake_blob):
        raw = m.group(1)
        d = raw.split("*")[0].rstrip("/")
        if not d:
            continue
        d = d.replace("${CMAKE_CURRENT_SOURCE_DIR}", "").replace("${CMAKE_SOURCE_DIR}", "").lstrip("/")
        if d:
            pats.append(re.compile(r"^" + re.escape(d) + r"(/|$)"))
    return pats


def lane_sources(root: Path) -> tuple[set[str], list[str]]:
    """Union of sources known to every configured lane."""
    known: set[str] = set()
    lanes_used: list[str] = []
    lane_dirs = sorted(glob.glob(str(root / "build/chronon/*"))) + \
                sorted(glob.glob(str(root / ".tmp/chronon-builds/*")))
    for lane in lane_dirs:
        lane_path = Path(lane)
        compdb = lane_path / "compile_commands.json"
        used = False
        if compdb.is_file():
            try:
                for entry in json.loads(compdb.read_text(encoding="utf-8", errors="replace")):
                    f = entry.get("file", "")
                    if f and not f.startswith(lane):
                        known.add(os.path.relpath(f, root) if os.path.isabs(f) else f)
                        used = True
            except (json.JSONDecodeError, OSError):
                pass
        # Unity builds compile a generated unity_*.cxx that includes the real
        # sources; those includes carry absolute paths back into the repo.
        for unity in lane_path.glob("**/CMakeFiles/*/Unity/*.cxx"):
            try:
                txt = unity.read_text(encoding="utf-8", errors="replace")
            except OSError:
                continue
            for m in re.finditer(r'#include\s+"([^"]+)"', txt):
                p = m.group(1).replace("\\\\", "/")
                if "/" in p:
                    candidate = os.path.normpath(p)
                    try:
                        rel = os.path.relpath(candidate, root)
                    except ValueError:
                        continue
                    if not rel.startswith(".."):
                        known.add(rel)
                        used = True
        if used:
            lanes_used.append(str(lane_path.relative_to(root)))
    return known, lanes_used


def include_index(root: Path, tracked: list[str]) -> set[str]:
    """Headers referenced through #include from any tracked text file."""
    referenced: set[str] = set()
    headers = {f for f in tracked if Path(f).suffix in HEADER_EXTS}
    inc_rx = re.compile(r'#\s*include\s*[<"]([^">]+)[">]')
    for f in tracked:
        if Path(f).suffix in DOC_EXTS or f.startswith(("docs/", "reports/")):
            continue
        try:
            txt = (root / f).read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        base = os.path.dirname(f)
        for m in inc_rx.finditer(txt):
            spec = m.group(1)
            # Includer-relative resolution first: a basename collision cannot
            # mask a genuinely unreferenced header.
            candidates = (
                os.path.normpath(os.path.join(base, spec)),
                os.path.normpath(os.path.join("include", spec)),
                os.path.normpath(spec),
                os.path.normpath(os.path.join("src", spec)),
                os.path.normpath(os.path.join("tests", spec)),
            )
            for cand in candidates:
                if cand in headers:
                    referenced.add(cand)
                    break
    return referenced


def git_grep_basename(root: Path, name: str) -> list[str]:
    out = sh(["git", "grep", "-l", "-F", name, "--",
              ":(exclude)build", ":(exclude).tmp"], root)
    return [l for l in out.splitlines() if l]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    ap.add_argument("--json", default="")
    ap.add_argument("--strict", action="store_true")
    ap.add_argument("--no-headers", action="store_true")
    args = ap.parse_args()

    root = Path(args.root).resolve()
    tracked = tracked_files(root)
    cmake_names, cmake_blob = cmake_text_refs(root, tracked)
    globs = glob_covered_dirs(cmake_blob, root)
    lane_known, lanes_used = lane_sources(root)
    inc_refs = set() if args.no_headers else include_index(root, tracked)

    universe = [f for f in tracked if is_universe_file(f, SOURCE_EXTS | (set() if args.no_headers else HEADER_EXTS))]

    report = {
        "root": str(root),
        "lanes": lanes_used,
        "tracked_files": len(tracked),
        "universe": len(universe),
        "verdicts": {"REFERENCED": 0, "DOC_ONLY": 0, "GLOB_COVERED": 0, "ORPHAN": 0},
        "orphans": [],
        "doc_only": [],
        "glob_covered": [],
        "referenced_sample": [],
    }

    for f in universe:
        ext = Path(f).suffix
        basename = os.path.basename(f)
        rel_names = {f, basename, f"/{basename}", basename + "\""}
        in_cmake = any(n in cmake_names for n in rel_names) or f in cmake_blob
        in_lane = f in lane_known or (ext in HEADER_EXTS and f in inc_refs)
        if in_cmake or in_lane:
            report["verdicts"]["REFERENCED"] += 1
            if len(report["referenced_sample"]) < 15:
                report["referenced_sample"].append(f)
            continue
        if any(p.search(f) for p in globs):
            report["verdicts"]["GLOB_COVERED"] += 1
            report["glob_covered"].append(f)
            continue
        # Candidate: fall back to a repository-wide textual search.
        hits = git_grep_basename(root, basename)
        non_doc = [h for h in hits if not h.startswith(DOC_PREFIXES) and h != f]
        doc_hits = [h for h in hits if h.startswith(DOC_PREFIXES)]
        if non_doc:
            report["verdicts"]["REFERENCED"] += 1
            report["referenced_sample"].append(f"{f}  (textual: {non_doc[0]})")
        elif doc_hits:
            report["verdicts"]["DOC_ONLY"] += 1
            report["doc_only"].append({"file": f, "docs": doc_hits[:4]})
        else:
            report["verdicts"]["ORPHAN"] += 1
            report["orphans"].append(f)

    v = report["verdicts"]
    print(f"source_census: {report['universe']} files "
          f"({report['tracked_files']} tracked) across {len(lanes_used)} lanes")
    print(f"  REFERENCED={v['REFERENCED']}  GLOB_COVERED={v['GLOB_COVERED']}  "
          f"DOC_ONLY={v['DOC_ONLY']}  ORPHAN={v['ORPHAN']}")
    for f in report["orphans"]:
        print(f"  ORPHAN       {f}")
    for item in report["doc_only"]:
        print(f"  DOC_ONLY     {item['file']}  <- {', '.join(item['docs'])}")
    for f in report["glob_covered"]:
        print(f"  GLOB_COVERED {f}")

    if args.json:
        Path(args.json).write_text(json.dumps(report, indent=1), encoding="utf-8")
        print(f"json written: {args.json}")

    if args.strict and report["orphans"]:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
