#!/usr/bin/env python3
"""
check_architecture.py — unified architecture rules engine.

Reads tools/architecture_rules.toml (the single source of truth) and enforces
every declarative rule category.  Replaces ~10 separate check_*.sh scripts
and the 935-line check_architecture_boundaries.sh.

Usage:
    python3 tools/check_architecture.py [--root REPO_ROOT] [--rules RULES_FILE]
    python3 tools/check_architecture.py --list  # list all rule names

Exit codes:
    0 = all rules PASS
    1 = at least one rule FAIL
    2 = configuration error (missing rules file, invalid TOML)
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from collections import defaultdict
from pathlib import Path
from typing import Any


# ── TOML support (Python 3.11+ has stdlib tomllib; fall back to tomli) ──────
try:
    import tomllib  # type: ignore[import-untyped]
except ImportError:
    try:
        import tomli as tomllib  # type: ignore[import-untyped,no-redef]
    except ImportError:
        print("GATE_FAIL_INTERNAL: neither tomllib (py3.11+) nor tomli is available", file=sys.stderr)
        print("  Install: pip install tomli", file=sys.stderr)
        sys.exit(2)


# ── Tool detection ──────────────────────────────────────────────────────────
def find_ripgrep() -> str | None:
    """Return the path to `rg` if available, else None."""
    try:
        subprocess.run(["rg", "--version"], capture_output=True, check=True)
        return "rg"
    except (FileNotFoundError, subprocess.CalledProcessError):
        return None


def find_grep() -> str:
    """Return path to GNU grep (or fallback to POSIX grep)."""
    return "grep"


RG = find_ripgrep()
GREP = find_grep()


# ── Helpers ──────────────────────────────────────────────────────────────────
def run_grep(pattern: str, paths: list[str], *, word_regexp: bool = False) -> list[str]:
    """Run ripgrep (preferred) or grep and return matching lines."""
    args: list[str] = []
    if RG:
        args = [RG, "-n", "--no-heading", "-t", "cpp"]
        if word_regexp:
            args.append("-w")
        args.extend(["-g", "!tools/check_architecture.py"])
        args.extend(["-g", "!tools/architecture_rules.toml"])
        args.append(pattern)
        args.extend(paths)
    else:
        args = [GREP, "-rnE", "--include=*.hpp", "--include=*.cpp", "--include=*.h"]
        if word_regexp:
            args.append("-w")
        args.append(pattern)
        args.extend(paths)

    try:
        result = subprocess.run(args, capture_output=True, text=True, timeout=60)
        return [line for line in result.stdout.splitlines() if line.strip()]
    except (subprocess.TimeoutExpired, FileNotFoundError):
        return []


def run_grep_multiline(pattern: str, paths: list[str]) -> list[str]:
    """Run ripgrep with --multiline for cross-line patterns."""
    if not RG:
        return []  # multiline not supported without rg
    args = [
        RG, "-n", "--no-heading", "--multiline",
        "-t", "cpp", pattern, *paths,
    ]
    try:
        result = subprocess.run(args, capture_output=True, text=True, timeout=60)
        return [line for line in result.stdout.splitlines() if line.strip()]
    except (subprocess.TimeoutExpired, FileNotFoundError):
        return []


def run_grep_any(paths: list[str], pattern: str) -> list[str]:
    """Run grep across any file type (not just C++)."""
    if RG:
        args = [RG, "-n", "--no-heading", pattern, *paths]
    else:
        args = [GREP, "-rnE", pattern, *paths]
    try:
        result = subprocess.run(args, capture_output=True, text=True, timeout=60)
        return [line for line in result.stdout.splitlines() if line.strip()]
    except (subprocess.TimeoutExpired, FileNotFoundError):
        return []


def git_ls_files(path: str) -> list[str]:
    """List tracked files matching `path/` in the git index."""
    try:
        result = subprocess.run(
            ["git", "ls-files", f"{path}/"],
            capture_output=True, text=True, timeout=10,
        )
        return [line for line in result.stdout.splitlines() if line.strip()]
    except (subprocess.TimeoutExpired, FileNotFoundError):
        return []


def git_grep_cached(pattern: str) -> list[str]:
    """Git grep in cached (tracked) files only."""
    try:
        result = subprocess.run(
            ["git", "grep", "-l", "--cached", "-E", "--", pattern],
            capture_output=True, text=True, timeout=30,
        )
        return [line for line in result.stdout.splitlines() if line.strip()]
    except (subprocess.TimeoutExpired, FileNotFoundError):
        return []


def comment_only(line: str) -> bool:
    """True if the line is only a comment (// or /* style)."""
    stripped = line.strip()
    return stripped.startswith("//") or stripped.startswith("/*") or stripped.startswith("*")


def strip_cpp_comments(line: str) -> str:
    """Remove // comments from a line (preserve code before //)."""
    in_string = False
    for i, ch in enumerate(line):
        if ch == '"' and (i == 0 or line[i - 1] != '\\'):
            in_string = not in_string
        if not in_string and i + 1 < len(line) and line[i:i + 2] == '//':
            return line[:i]
    return line


def filter_code_only(matches: list[str], symbol: str) -> list[str]:
    """Filter out comment-only lines and lines where symbol appears only in trailing comment."""
    result = []
    for match_line in matches:
        # Match format: path:lineno:text
        parts = match_line.split(":", 2)
        if len(parts) < 3:
            continue
        text = parts[2]
        if comment_only(text):
            continue
        code = strip_cpp_comments(text)
        if symbol not in code:
            continue
        result.append(match_line)
    return result


def filter_allow_list(matches: list[str], allow_list: list[str]) -> list[str]:
    """Exclude matches where the file path matches any allow_list pattern."""
    result = []
    for match_line in matches:
        file_path = match_line.split(":")[0]
        if any(re.search(pat, file_path) for pat in allow_list):
            continue
        result.append(match_line)
    return result


def filter_allow_symbol(matches: list[str], allow_symbols: list[str]) -> list[str]:
    """Exclude lines that contain any allowlisted symbol variant."""
    result = []
    for match_line in matches:
        if any(re.search(sym, match_line) for sym in allow_symbols):
            continue
        result.append(match_line)
    return result


def count_pattern(root: Path, paths: list[str], pattern: str) -> int:
    """Count source lines matching a legacy census pattern."""
    compiled = re.compile(pattern)
    count = 0
    for rel in paths:
        base = root / rel
        if not base.exists():
            continue
        files = [base] if base.is_file() else base.rglob("*")
        for path in files:
            if path.suffix not in {".cpp", ".hpp", ".h"}:
                continue
            try:
                count += sum(1 for line in path.read_text(errors="replace").splitlines()
                             if compiled.search(strip_cpp_comments(line)))
            except OSError:
                continue
    return count


# ── Rule enforcement ────────────────────────────────────────────────────────
class GateRunner:
    """Reads architecture_rules.toml and enforces every rule."""

    def __init__(self, root: Path, rules_path: Path):
        self.root = root
        self.rules_path = rules_path
        with open(rules_path, "rb") as f:
            self.rules: dict[str, Any] = tomllib.load(f)
        self.failures: list[str] = []
        self.passes: list[str] = []

    def _resolve_path(self, path: str) -> Path:
        return self.root / path

    def _resolve_scan_paths(self, rule: dict[str, Any]) -> list[str]:
        """Resolve scan_paths from rule, defaulting to active source dirs."""
        raw = rule.get("scan_paths", ["include", "src", "tests", "apps"])
        result = []
        for p in raw:
            full = self._resolve_path(p)
            if full.exists():
                result.append(str(full))
        return result

    def _fail(self, name: str, detail: str) -> None:
        print(f"  [FAIL] {name}: {detail}")
        self.failures.append(f"{name}: {detail}")

    def _pass(self, name: str, detail: str = "") -> None:
        msg = f"  [PASS] {name}"
        if detail:
            msg += f" — {detail}"
        print(msg)
        self.passes.append(name)

    def check_legacy_census(self) -> None:
        """Report the two historical prevalence censuses in one place.

        These are informational inventories, matching the old forward-only
        shell gates. Hard architectural prohibitions remain declarative TOML
        rules and are enforced as failures by the normal rule categories.
        """
        print("=== Legacy Census (informational) ===")
        asset = sum((
            count_pattern(self.root, ["content", "src/scene"],
                          r"\b(?:font_path|image_path|video_path|audio_path)\b"),
            count_pattern(self.root, ["src/scene", "content"],
                          r"\b(?:resolve_handle|load_image|decode_video|decode_audio|font_engine\.load)\b"),
        ))
        timeline = sum((
            count_pattern(self.root, ["content"], r"\blayer\.(?:from|duration)\b"),
            count_pattern(self.root, ["content", "src/animation", "src/text"],
                          r"\bsample\(\s*(?:ctx\.frame|frame_context\.frame|global_frame)\b"),
            count_pattern(self.root, ["content", "src/scene"],
                          r"(?:^|[^A-Za-z0-9_.])duration\s*=\s*[01]\b"),
        ))
        self._pass("legacy_census", f"asset={asset} timeline={timeline} (informational)")

    # ── Forbidden Path ──────────────────────────────────────────────────
    def check_forbidden_path(self, rule: dict[str, Any]) -> None:
        name = rule["name"]
        for rel_path in rule["paths"]:
            full = self._resolve_path(rel_path)
            if full.exists():
                self._fail(name, f"retired file exists: {rel_path}")
                if hint := rule.get("hint"):
                    print(f"         hint: {hint}")
                return
        self._pass(name)

    # ── Forbidden Symbol ────────────────────────────────────────────────
    def check_forbidden_symbol(self, rule: dict[str, Any]) -> None:
        name = rule["name"]
        patterns = rule["patterns"]
        scan_paths = self._resolve_scan_paths(rule)
        skip_comments = rule.get("skip_comments", False)
        allow_list = rule.get("allow_list", [])
        allow_symbols = rule.get("allow_list_for_symbol", [])

        for pattern in patterns:
            matches = run_grep(pattern, scan_paths)
            if ok := self._resolve_exclude_paths(rule):
                matches = [m for m in matches if not any(re.search(e, m.split(":")[0]) for e in ok)]
            if skip_comments:
                matches = filter_code_only(matches, pattern)
            if allow_list:
                matches = filter_allow_list(matches, allow_list)
            if allow_symbols:
                matches = filter_allow_symbol(matches, allow_symbols)

            if matches:
                self._fail(name, f"pattern '{pattern}' found {len(matches)} match(es):")
                for m in matches[:5]:
                    print(f"         {m}")
                if len(matches) > 5:
                    print(f"         ... and {len(matches) - 5} more")
                if hint := rule.get("hint"):
                    print(f"         hint: {hint}")
                return
        self._pass(name)

    def _resolve_exclude_paths(self, rule: dict[str, Any]) -> list[str]:
        raw = rule.get("exclude_paths", [])
        return [re.escape(p) for p in raw]

    # ── Forbidden Include ───────────────────────────────────────────────
    def check_forbidden_include(self, rule: dict[str, Any]) -> None:
        name = rule["name"]
        patterns = rule["patterns"]
        scan_paths = self._resolve_scan_paths(rule)
        skip_comments = rule.get("skip_comments", False)

        for pattern in patterns:
            matches = run_grep(pattern, scan_paths)
            if exclusions := self._resolve_exclude_paths(rule):
                matches = [m for m in matches
                           if not any(re.search(e, m.split(":", 1)[0])
                                      for e in exclusions)]
            if skip_comments:
                # For include patterns, a match inside a comment is a comment
                # ABOUT the forbidden include, not the include itself.
                matches = [m for m in matches if not comment_only(m.split(":", 2)[-1])]
            if matches:
                self._fail(name, f"forbidden include pattern found: '{pattern}' ({len(matches)} match(es))")
                for m in matches[:5]:
                    print(f"         {m}")
                if hint := rule.get("hint"):
                    print(f"         hint: {hint}")
                return
        self._pass(name)

    # ── Unique Source Owner ─────────────────────────────────────────────
    def check_unique_source_owner(self, rule: dict[str, Any]) -> None:
        name = rule["name"]
        canon_pattern = rule["canonical_pattern"]
        canon_path = rule["canonical_path"]
        expected = rule.get("expected_count", 1)
        legacy_patterns = rule.get("legacy_patterns", [])
        soft_cap = rule.get("legacy_soft_cap", 0)
        scan_paths = self._resolve_scan_paths(rule)

        # Count canonical definitions
        canon_matches = run_grep(canon_pattern, scan_paths)
        canon_files: set[str] = {m.split(":")[0] for m in canon_matches}
        canon_count = len(canon_files)

        # Count legacy patterns
        legacy_files: set[str] = set()
        for pat in legacy_patterns:
            m = run_grep(pat, scan_paths)
            for line in m:
                f = line.split(":")[0]
                if f != str(self._resolve_path(canon_path)):
                    legacy_files.add(f)

        legacy_count = len(legacy_files)

        if canon_count >= expected and legacy_count <= soft_cap:
            extra = ""
            if legacy_count > 0:
                extra = f"; {legacy_count} legacy within soft-cap={soft_cap}"
            self._pass(name, f"canonical at {canon_path} (count={canon_count}{extra})")
        else:
            reason = f"canonical={canon_count} (need ≥{expected}), legacy={legacy_count} (cap={soft_cap})"
            self._fail(name, reason)
            if hint := rule.get("hint"):
                print(f"         hint: {hint}")

    # ── Boundary ────────────────────────────────────────────────────────
    def check_boundary(self, rule: dict[str, Any]) -> None:
        name = rule["name"]
        patterns = rule["forbidden_patterns"]
        scan_paths = self._resolve_scan_paths(rule)

        for pattern in patterns:
            matches = run_grep_any(scan_paths, pattern)
            if matches:
                self._fail(name, f"boundary violation: '{pattern}' in {scan_paths}")
                for m in matches[:5]:
                    print(f"         {m}")
                if hint := rule.get("hint"):
                    print(f"         hint: {hint}")
                return
        self._pass(name)

    # ── Contract ────────────────────────────────────────────────────────
    def check_contract(self, rule: dict[str, Any]) -> None:
        name = rule["name"]
        optional_file = rule.get("optional", False)

        # Special case: effect processor coverage
        if "effect_catalog" in rule:
            catalog_path = self._resolve_path(rule["effect_catalog"])
            registry_path = self._resolve_path(rule["processor_registry"])
            if not catalog_path.exists():
                self._fail(name, f"effect catalog missing: {rule['effect_catalog']}")
                return
            if not registry_path.exists():
                self._fail(name, f"processor registry missing: {rule['processor_registry']}")
                return

            catalog_text = catalog_path.read_text(encoding="utf-8")
            registry_text = registry_path.read_text(encoding="utf-8")

            catalog_params = re.findall(
                r"CHRONON_EFFECT\(\s*\d+\s*,\s*\w+\s*,\s*(\w+)", catalog_text
            )
            registered_params = set(
                re.findall(r"register_effect_processor<\s*(\w+)\s*>", registry_text)
            )

            duplicates = sorted(n for n in set(catalog_params) if catalog_params.count(n) > 1)
            missing = sorted(set(catalog_params) - registered_params)
            extra = sorted(registered_params - set(catalog_params))

            if duplicates or missing or extra:
                details = []
                if duplicates:
                    details.append(f"duplicates: {', '.join(duplicates)}")
                if missing:
                    details.append(f"missing: {', '.join(missing)}")
                if extra:
                    details.append(f"extra: {', '.join(extra)}")
                self._fail(name, "; ".join(details))
            else:
                self._pass(name, f"{len(catalog_params)} effects registered")
            return

        # Standard contract: check patterns in a file
        file_path = self._resolve_path(rule["file"])
        if not file_path.exists():
            if optional_file:
                self._pass(name, "file absent (optional, vacuous)")
            else:
                self._fail(name, f"file missing: {rule['file']}")
            return

        content = file_path.read_text(encoding="utf-8")
        required = rule.get("required_pattern", None)
        required_list = rule.get("required_patterns", [])

        if required:
            required_list = [required]

        if not required_list:
            self._pass(name, "no required patterns (vacuous)")
            return

        # For multi-line patterns, flatten the file
        for pattern in required_list:
            flat = content
            if "\n" not in pattern:
                flat = content.replace("\n", " ")
            if not re.search(pattern, flat):
                self._fail(name, f"required pattern not found: '{pattern}'")
                if hint := rule.get("hint"):
                    print(f"         hint: {hint}")
                return
        self._pass(name)

    # ── LOC Bound ───────────────────────────────────────────────────────
    def check_loc_bound(self, rule: dict[str, Any]) -> None:
        name = rule["name"]
        file_path = self._resolve_path(rule["file"])

        if not file_path.exists():
            self._pass(name, "file absent (vacuous)")
            return

        content = file_path.read_text(encoding="utf-8")
        lines = content.splitlines()

        if "max_loc" in rule:
            loc = len(lines)
            max_loc = rule["max_loc"]
            if loc > max_loc:
                self._fail(name, f"LOC={loc} > max={max_loc}")
                return

        if "max_non_local_includes" in rule:
            nli = sum(1 for line in lines if re.match(r'#include\s*<(chronon3d|backends)/', line))
            max_nli = rule["max_non_local_includes"]
            if nli > max_nli:
                self._fail(name, f"non-local includes={nli} > max={max_nli}")
                return

        self._pass(name, f"LOC={len(lines)}" if "max_loc" in rule else "OK")

    # ── CMake Ownership ─────────────────────────────────────────────────
    def check_cmake_ownership(self) -> None:
        cmake_rules = self.rules.get("cmake_ownership")
        if not cmake_rules:
            return

        # Collect all source-file → target mappings
        ownership: dict[str, set[str]] = defaultdict(set)
        command_re = re.compile(r"\b(add_library|add_executable|target_sources)\s*\(", re.I)
        keywords = {
            "STATIC", "SHARED", "MODULE", "OBJECT", "INTERFACE", "IMPORTED", "ALIAS",
            "WIN32", "MACOSX_BUNDLE", "EXCLUDE_FROM_ALL", "PRIVATE", "PUBLIC",
        }

        for cmake_file in self.root.rglob("CMakeLists.txt"):
            parts = set(cmake_file.parts)
            if parts & {"build", "out", ".tmp", ".git", "vcpkg_bootstrap", "vcpkg_installed"}:
                continue
            text = re.sub(r"#.*", "", cmake_file.read_text(encoding="utf-8"))
            for match in command_re.finditer(text):
                depth = 1
                i = match.end()
                while i < len(text) and depth:
                    if text[i] == "(":
                        depth += 1
                    elif text[i] == ")":
                        depth -= 1
                    i += 1
                if depth:
                    continue
                args_raw = text[match.end():i - 1]
                toks = re.findall(r'"([^"]+)"|([^\s]+)', args_raw)
                args = [q or b for q, b in toks]
                if not args:
                    continue
                target = args[0]
                for raw in args[1:]:
                    if raw.upper() in keywords:
                        continue
                    if "$<" in raw or "${" in raw:
                        continue
                    if not raw.lower().endswith((".cpp", ".cxx", ".cc")):
                        continue
                    candidate = Path(raw)
                    if not candidate.is_absolute():
                        candidate = cmake_file.parent / candidate
                    candidate = candidate.resolve()
                    if candidate.is_file():
                        ownership[str(candidate.relative_to(self.root))].add(target)

        # Check for multi-owner violations
        allowed = {
            tuple(e["path"].split("/")): frozenset(e["owners"])
            for e in cmake_rules.get("allowed_multi_owner", [])
        }

        conflicts = []
        for path, targets in ownership.items():
            if len(targets) <= 1:
                continue
            parts = tuple(path.split("/"))
            if parts in allowed and frozenset(targets) == allowed[parts]:
                continue
            conflicts.append((path, sorted(targets)))

        if conflicts:
            print(f"  [FAIL] cmake_ownership: {len(conflicts)} source(s) have multiple owners:")
            for p, t in conflicts[:10]:
                print(f"         {p} → {', '.join(t)}")
            if len(conflicts) > 10:
                print(f"         ... and {len(conflicts) - 10} more")
            self._fail("cmake_ownership", f"{len(conflicts)} multi-owner violations")
        else:
            self._pass("cmake_ownership", f"{len(ownership)} sources have unique owners")

    # ── Gitignored ──────────────────────────────────────────────────────
    def check_gitignored(self) -> None:
        git_rules = self.rules.get("gitignored")
        if not git_rules:
            return

        total_violations = 0

        # Check directories
        for d in git_rules.get("dirs", []):
            tracked = git_ls_files(d)
            if tracked:
                print(f"  [FAIL] gitignored: {d}/ has {len(tracked)} tracked entries:")
                for t in tracked[:3]:
                    print(f"         {t}")
                self.failures.append(f"gitignored: {d}/ has tracked entries")
                total_violations += len(tracked)

        # Check build globs
        for pattern in git_rules.get("build_globs", []):
            for real in self.root.glob(pattern):
                if real.is_dir():
                    tracked = git_ls_files(str(real.relative_to(self.root)))
                    if tracked:
                        print(f"  [FAIL] gitignored (glob): {real.name}/ has {len(tracked)} tracked entries")
                        self.failures.append(f"gitignored (glob): {real.name}/")
                        total_violations += len(tracked)

        # Check file patterns
        for pattern in git_rules.get("file_patterns", []):
            for match_path in self.root.glob(pattern.lstrip("/")):
                if match_path.is_file():
                    try:
                        subprocess.run(
                            ["git", "ls-files", "--error-unmatch", str(match_path.relative_to(self.root))],
                            capture_output=True, check=True,
                        )
                        print(f"  [FAIL] gitignored (file): {match_path.relative_to(self.root)} is tracked")
                        self.failures.append(f"gitignored (file): {match_path.relative_to(self.root)}")
                        total_violations += 1
                    except subprocess.CalledProcessError:
                        pass

        # Check tmp gate globs
        for pattern in git_rules.get("tmp_gate_globs", []):
            for real in self.root.glob(pattern):
                if real.is_dir():
                    tracked = git_ls_files(str(real.relative_to(self.root)))
                    if tracked:
                        print(f"  [FAIL] gitignored (tmp-gate): {real.name}/ has {len(tracked)} tracked entries")
                        self.failures.append(f"gitignored (tmp-gate): {real.name}/")
                        total_violations += len(tracked)

        # Absolute path leak detection
        abs_pattern = git_rules.get("absolute_path_pattern", "")
        if abs_pattern:
            hits = git_grep_cached(abs_pattern)
            if hits:
                print(f"  [FAIL] gitignored (abs-path): {len(hits)} tracked files contain absolute paths")
                for h in hits[:5]:
                    print(f"         {h}")
                self.failures.append(f"gitignored (abs-path): {len(hits)} files")
                total_violations += len(hits)

        if total_violations == 0:
            self._pass("gitignored", "no tracked entries in ignored dirs")
        else:
            self._fail("gitignored", f"{total_violations} total violations")

    # ── Run all ─────────────────────────────────────────────────────────
    def run(self) -> int:
        print(f"=== Architecture Rules Engine ===")
        print(f"    rules file: {self.rules_path}")
        print(f"    repo root:  {self.root}")
        print()

        # Collect rule lists
        forbidden_paths = self.rules.get("forbidden_path", [])
        forbidden_symbols = self.rules.get("forbidden_symbol", [])
        forbidden_includes = self.rules.get("forbidden_include", [])
        unique_owners = self.rules.get("unique_source_owner", [])
        boundaries = self.rules.get("boundary", [])
        contracts = self.rules.get("contract", [])
        loc_bounds = self.rules.get("loc_bound", [])

        # Enforce each category
        for rule in forbidden_paths:
            self.check_forbidden_path(rule)

        print()
        for rule in forbidden_symbols:
            self.check_forbidden_symbol(rule)

        print()
        for rule in forbidden_includes:
            self.check_forbidden_include(rule)

        print()
        print("=== Unique Source Owner Audits ===")
        for rule in unique_owners:
            self.check_unique_source_owner(rule)

        print()
        print("=== Boundary Rules ===")
        for rule in boundaries:
            self.check_boundary(rule)

        print()
        print("=== Contract Checks ===")
        for rule in contracts:
            self.check_contract(rule)

        print()
        for rule in loc_bounds:
            self.check_loc_bound(rule)

        print()
        self.check_legacy_census()

        print()
        print("=== CMake Source Ownership ===")
        self.check_cmake_ownership()

        print()
        print("=== Gitignored Enforcement ===")
        self.check_gitignored()

        # Summary
        print()
        total = len(self.passes) + len(self.failures)
        gate_name = "check_architecture"
        if self.failures:
            print(f"GATE_FAIL: {len(self.failures)}/{total} architecture rule(s) FAILED")
            for f in self.failures:
                print(f"  - {f}")
            return 1

        print(f"GATE_PASS: {len(self.passes)}/{total} architecture rules PASSED")
        print(f"[INFO] {gate_name}: all declarative rules verified (TOML rules file: {self.rules_path.name})")
        return 0


# ── Main ─────────────────────────────────────────────────────────────────────
def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check architecture rules from declarative TOML (replaces ~10 gate scripts).",
    )
    parser.add_argument(
        "--root", default=".", type=Path,
        help="Repository root (default: current directory).",
    )
    parser.add_argument(
        "--rules", default=None, type=Path,
        help="Path to architecture_rules.toml (default: tools/architecture_rules.toml).",
    )
    parser.add_argument(
        "--list", action="store_true",
        help="List all rule names and exit.",
    )
    args = parser.parse_args()

    root = args.root.resolve()

    if args.rules:
        rules_path = args.rules.resolve()
    else:
        rules_path = root / "tools" / "architecture_rules.toml"
        if not rules_path.exists():
            # Try relative to script
            rules_path = Path(__file__).resolve().parent / "architecture_rules.toml"

    if not rules_path.exists():
        print(f"GATE_FAIL_INTERNAL: rules file not found: {rules_path}", file=sys.stderr)
        return 2

    if args.list:
        with open(rules_path, "rb") as f:
            rules = tomllib.load(f)
        for category in ["forbidden_path", "forbidden_symbol", "forbidden_include",
                         "unique_source_owner", "boundary", "contract", "loc_bound",
                         "cmake_ownership", "gitignored"]:
            entries = rules.get(category, [])
            if isinstance(entries, list):
                print(f"\n[{category}] ({len(entries)} rules):")
                for e in entries:
                    print(f"  - {e['name']}")
            elif isinstance(entries, dict):
                print(f"\n[{category}]: {entries.get('description', '(no description)')}")
        return 0

    runner = GateRunner(root, rules_path)
    return runner.run()


if __name__ == "__main__":
    sys.exit(main())
