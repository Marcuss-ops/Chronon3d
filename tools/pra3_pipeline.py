#!/usr/bin/env python3
"""
End-to-end PR-A3 amend pipeline for Chronon3d.

Steps (each is gated by the previous; any failure aborts):
  1. python syntax-verify tools/pra3_apply.py
  2. python3 tools/pra3_apply.py   (applies A-G bundle on disk)
  3. forbidden-API grep on working tree (must be zero)
  4. positive-API grep on working tree (must be >= expected counts)
  5. git add -A + write commit msg + git commit --amend (ASCII subject)
  6. git fetch origin; git push --force-with-lease origin HEAD:main
  7. git log -n 3 --oneline; git status -sb

Usage:
  python3 tools/pra3_pipeline.py

Exit codes:
  0 = success, push landed
  non-zero = any step failed (script aborts before push)
"""
from __future__ import annotations
import os
import pathlib
import subprocess
import sys
import shutil

REPO = pathlib.Path("/home/pierone/Pyt/Chronon3d")
APPLY = REPO / "tools" / "pra3_apply.py"
TEST_FILE = REPO / "tests" / "deterministic" / "test_visual_regression_scenarios.cpp"
CACHE = REPO / "tests" / "deterministic_tests.cmake"
COMMIT_MSG = REPO / "tools" / "_commitmsg_pr_a3_amend.txt"

FORBIDDEN = [
    "Color::navy(",
    "Color::crimson(",
    "appearance.stroke.enabled",
    "appearance.stroke.width",
    "appearance.stroke.color",
    "appearance.gradient.kind",
    "appearance.gradient.angle_deg",
    "appearance.gradient.stops",
    "TextGradientKind",
    "::chronon3d::test::is_reference_captured",
    "inner_radius",
    "bloom_radius",
    "inner_intensity",
    "bloom_intensity",
    "constexpr int kVFps = 30",
    "480.0f, Color::crimson",
]

POSITIVE_MIN = {
    "fill hdr include":  (1, "include/fill hdr"),
    "Fill::linear":      (1, "Fill::linear usage"),
    "paint.stroke_enabled":     (1, "stroke_enabled"),
    "paint.stroke_width ":      (1, "stroke_width "),
    "paint.stroke_color":       (1, "stroke_color"),
    "max_lines=0 param":   (1, "max_lines=0"),
    "max_lines=4 call":    (1, "max_lines=4"),
    "220.0f font":  (1, "220.0f font shrink"),
    "Color navy literal":  (1, "navy literal"),
    "Color crimson literal": (1, "crimson literal"),
    "GlowParams 3-field": (1, "GlowParams 3-field"),
    "unqualified is_reference_captured(kref) in macro": (1, "macro unqualified"),
}

def step(title: str) -> None:
    bar = "=" * 70
    print(f"\n{bar}\n[STEP] {title}\n{bar}", flush=True)

def fail(msg: str) -> None:
    print(f"\n[FAIL] {msg}", flush=True)
    sys.exit(1)

def ok(msg: str) -> None:
    print(f"[OK]   {msg}", flush=True)

def sh(cmd: list[str], *, capture: bool = False, check: bool = True) -> str:
    """Run shell command. Returns stdout str. Raises on non-zero exit if check."""
    result = subprocess.run(
        cmd,
        cwd=str(REPO),
        capture_output=capture,
        text=True,
        env={**os.environ, "LC_ALL": "C.UTF-8", "LANG": "C.UTF-8"},
    )
    if check and result.returncode != 0:
        out = (result.stdout or "") + (result.stderr or "")
        fail(f"shell cmd failed: {' '.join(cmd)}\nrc={result.returncode}\n{out[-1500:]}")
    return result.stdout if capture else ""

# --------------------------- step 1: syntax verify ---------------------------
step("1. python syntax verify tools/pra3_apply.py")
try:
    import ast
    ast.parse(APPLY.read_text(encoding="utf-8"))
    ok("tools/pra3_apply.py parses")
except SyntaxError as e:
    fail(f"apply script SyntaxError: {e}")

# --------------------------- step 2: apply recipe ----------------------------
step("2. python3 tools/pra3_apply.py")
sh(["python3", str(APPLY)], capture=True)
ok("apply recipe exit 0")

# --------------------------- step 3: forbidden grep --------------------------
step("3. forbidden-API grep on working tree (must be zero)")
text = TEST_FILE.read_text(encoding="utf-8")
forbidden_hits: list[tuple[str, int]] = []
for needle in FORBIDDEN:
    for lineno, line in enumerate(text.splitlines(), start=1):
        if needle in line:
            forbidden_hits.append((needle, lineno))
if forbidden_hits:
    print("Forbidden hits:")
    for n, ln in forbidden_hits:
        ctx = text.splitlines()[ln - 1][:120]
        print(f"  line {ln}: {n}  ::  {ctx}")
    fail(f"{len(forbidden_hits)} forbidden substring(s) remain")
ok("zero forbidden-API hits")

# --------------------------- step 4: positive markers ------------------------
step("4. positive-API grep on working tree (must be >= expected)")
missing: list[str] = []
for label, (needle, min_count) in POSITIVE_MIN.items():
    count = text.count(needle)
    status = "OK" if count >= min_count else "MISS"
    print(f"  [{status}] {label}:  needle={needle!r}  count={count}  (min={min_count})")
    if count < min_count:
        missing.append(label)
if missing:
    fail(f"missing positive markers: {missing}")
ok("all positive markers present")

# --------------------------- step 5: amend commit -----------------------------
step("5. write commit msg (ASCII) + git add -A + git commit --amend --no-edit is WRONG; we need to RE-WRITE the commit message")
# We will replace the existing message entirely so the amend has the ASCII-clean subject.
COMMIT_MSG.write_text(
    "tests+cmake(deterministic): PR-A3 amend (visual regression scenarios, ASCII-clean)\n\n"
    "Amends dbb34df4 to land the A-G API-fidelity bundle without G2 (banner dropped as brittle;\n"
    "setup follows: include Fill::linear transitively via builder_params -> shape -> fill,\n"
    "Color literals replace Color::navy/crimson, TextPaint is the canonical stroke home,\n"
    "Fill::linear carries the gradient, GlowParams loses inner/bloom fields,\n"
    "VR_GATE macro ditches ::chronon3d::test:: qualifier for anonymous-namespace lookup,\n"
    "CenterTextOptions gains max_lines param (default 0=unlimited; 0 4 means render N wrapped\n"
    "lines), §2 Multiline passes max_lines=4, §15 ScaleExtreme shrinks huge 480->220 pt and\n"
    "wide-anchors tiny/huge to NW/SE for zero glyph overlap, dead kVFps constant removed.\n\n"
    "All anchors were byte-verified against the committed dbb34df4 file; every change is\n"
    "tracked through tools/pra3_apply.py so future diffs are reproducible.\n",
    encoding="utf-8",
)
sh(["git", "add", str(TEST_FILE.relative_to(REPO)), str(CACHE.relative_to(REPO)), str(APPLY.relative_to(REPO)), str(COMMIT_MSG.relative_to(REPO))])
sh(["git", "commit", "--amend", "-F", str(COMMIT_MSG.relative_to(REPO))])
ok("git commit --amend landed")

# --------------------------- step 6: force-push -------------------------------
step("6. git fetch origin + git push --force-with-lease origin HEAD:main")
sh(["git", "fetch", "origin"], capture=True)
sh(["git", "push", "--force-with-lease", "origin", "HEAD:main"])
ok("force-pushed to origin/main")

# --------------------------- step 7: verify clean tree ------------------------
step("7. final git log + git status")
log = sh(["git", "log", "-n", "3", "--oneline"], capture=True)
status = sh(["git", "status", "-sb"], capture=True)
print(log)
print(status)

sha = sh(["git", "rev-parse", "HEAD"], capture=True).strip()
print(f"\nNEW HEAD: {sha}")
