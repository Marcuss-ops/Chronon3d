#!/usr/bin/env python3
from __future__ import annotations

import argparse
import fnmatch
import json
import os
import re
import subprocess
import sys
from collections import defaultdict
from pathlib import Path
from typing import Any, Iterable

try:
    import tomllib
except ImportError:
    import tomli as tomllib  # type: ignore[no-redef]

EXCLUDED_DIRS = {
    ".git", "build", "out", ".cache", ".tmp", ".worktrees", "node_modules",
    "vcpkg", "vcpkg_bootstrap", "vcpkg_installed", "__pycache__",
}
DEFAULT_SUFFIXES = {".h", ".hpp", ".hh", ".c", ".cc", ".cpp", ".cxx", ".inc"}


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


_COMMENT_TOKEN_RE = re.compile(
    r'"(?:\\.|[^"\\])*"'      # double-quoted string (kept)
    r"|'(?:\\.|[^'\\])*'"      # char/string literal (kept)
    r"|/\*.*?\*/"              # block comment
    r"|/\*.*\Z"                # unterminated block comment (EOF)
    r"|//[^\n]*",              # line comment
    re.DOTALL,
)


def strip_comments(text: str) -> str:
    """Regex equivalent of the previous per-character scanner.

    Comments are blanked out with spaces (newlines preserved so line/col
    structure and error line numbers stay identical); string/char literals
    are left untouched so comment markers inside them survive. The previous
    pure-Python loop cost ~2 minutes of CPU across the full scan — the gate
    was functionally hung on large checkouts.
    """
    def repl(m: re.Match[str]) -> str:
        s = m.group(0)
        if s.startswith("/"):
            return re.sub(r"[^\n]", " ", s)
        return s
    return _COMMENT_TOKEN_RE.sub(repl, text)


def iter_files(root: Path, paths: Iterable[str], extensions: list[str] | None = None) -> Iterable[Path]:
    suffixes = DEFAULT_SUFFIXES if not extensions else set(extensions)
    any_file = "*" in suffixes
    for rel in paths:
        base = root / rel
        if not base.exists():
            continue
        if base.is_file():
            candidates: Iterable[Path] = [base]
        else:
            # Prune excluded directories during traversal instead of filtering
            # after rglob: a 30+ GB build/ tree makes post-hoc filtering take
            # minutes, which is functionally a hung gate.
            def walk_pruned(base: Path = base) -> Iterable[Path]:
                for dirpath, dirnames, filenames in os.walk(base):
                    dirnames[:] = [d for d in dirnames
                                   if d not in EXCLUDED_DIRS and not d.startswith("build-")]
                    for name in filenames:
                        yield Path(dirpath) / name
            candidates = walk_pruned()
        for path in candidates:
            if not path.is_file():
                continue
            try:
                relative = path.relative_to(root)
            except ValueError:
                continue
            if any(part in EXCLUDED_DIRS or part.startswith("build-") for part in relative.parts[:-1]):
                continue
            if path.name == "CMakeLists.txt" and not any_file and ".txt" not in suffixes:
                continue
            if any_file or path.suffix in suffixes or path.name == "CMakeLists.txt":
                yield path


def pruned_rglob(base: Path, pattern: str) -> Iterable[Path]:
    """os.walk with directory pruning; equivalent to base.rglob(pattern) but
    never descends into excluded/build trees (30+ GB build/ makes rglob
    take minutes — a functionally hung gate)."""
    for dirpath, dirnames, filenames in os.walk(base):
        dirnames[:] = [d for d in dirnames
                       if d not in EXCLUDED_DIRS and not d.startswith("build-")]
        for name in filenames:
            if fnmatch.fnmatch(name, pattern):
                yield Path(dirpath) / name


def relpath(root: Path, path: Path) -> str:
    return path.relative_to(root).as_posix()


def path_matches(path: str, patterns: Iterable[str]) -> bool:
    return any(re.search(pattern, path) for pattern in patterns)


def line_matches_any(line: str, patterns: Iterable[str]) -> bool:
    return any(re.search(pattern, line) for pattern in patterns)


def merge_rules(dst: dict[str, Any], src: dict[str, Any]) -> None:
    for key, value in src.items():
        if key in {"meta", "includes"}:
            continue
        if isinstance(value, list):
            current = dst.setdefault(key, [])
            if not isinstance(current, list):
                dst[key] = value
                continue
            for item in value:
                if isinstance(item, dict) and "name" in item:
                    replaced = False
                    for index, existing in enumerate(current):
                        if isinstance(existing, dict) and existing.get("name") == item["name"]:
                            current[index] = item
                            replaced = True
                            break
                    if not replaced:
                        current.append(item)
                else:
                    current.append(item)
        elif isinstance(value, dict):
            current = dst.setdefault(key, {})
            if isinstance(current, dict):
                current.update(value)
            else:
                dst[key] = value
        else:
            dst[key] = value


def load_registry(path: Path, seen: set[Path] | None = None) -> dict[str, Any]:
    seen = seen or set()
    path = path.resolve()
    if path in seen:
        raise ValueError(f"architecture registry include cycle at {path}")
    seen.add(path)
    with path.open("rb") as fh:
        data: dict[str, Any] = tomllib.load(fh)
    merged: dict[str, Any] = {}
    includes = data.get("includes") or data.get("meta", {}).get("includes", [])
    for include in includes:
        load_path = path.parent / include
        if not load_path.exists():
            raise FileNotFoundError(f"architecture registry include missing: {load_path}")
        merge_rules(merged, load_registry(load_path, seen))
    merge_rules(merged, data.get("meta", {}))
    merge_rules(merged, data)
    seen.remove(path)
    return merged


class Runner:
    def __init__(self, root: Path, rules_path: Path, prefix: str | None = None):
        self.root = root
        self.rules_path = rules_path
        self.prefix = prefix
        self.rules = load_registry(rules_path)
        self.passes: list[str] = []
        self.failures: list[str] = []

    def selected(self, name: str) -> bool:
        return self.prefix is None or name.startswith(self.prefix)

    def ok(self, name: str, detail: str = "") -> None:
        if not self.selected(name):
            return
        suffix = f" — {detail}" if detail else ""
        print(f"  [PASS] {name}{suffix}")
        self.passes.append(name)

    def fail(self, name: str, detail: str) -> None:
        if not self.selected(name):
            return
        print(f"  [FAIL] {name}: {detail}")
        self.failures.append(f"{name}: {detail}")

    def scan_hits(self, rule: dict[str, Any]) -> list[str]:
        paths = rule.get("scan_paths", ["include", "src", "tests", "apps"])
        extensions = rule.get("extensions")
        allow_paths = list(rule.get("allow_list", [])) + list(rule.get("exclude_paths", []))
        allow_lines = list(rule.get("allow_line_patterns", [])) + list(rule.get("allow_list_for_symbol", []))
        skip_comments = rule.get("skip_comments", False)
        multiline = rule.get("multiline", False)
        patterns = [re.compile(p, re.MULTILINE | (re.DOTALL if multiline else 0))
                    for p in rule.get("patterns", rule.get("forbidden_patterns", []))]
        file_requires = rule.get("file_requires_pattern")
        requires_re = re.compile(file_requires, re.MULTILINE) if file_requires else None
        hits: list[str] = []
        for path in iter_files(self.root, paths, extensions):
            rel = relpath(self.root, path)
            if path_matches(rel, allow_paths):
                continue
            raw = read_text(path)
            text = strip_comments(raw) if skip_comments else raw
            if requires_re and not requires_re.search(text):
                continue
            if rule.get("cooccurrence_patterns"):
                if all(re.search(p, text, re.MULTILINE) for p in rule["cooccurrence_patterns"]):
                    hits.append(f"{rel}: co-occurrence {rule['cooccurrence_patterns']}")
                continue
            if multiline:
                for rx in patterns:
                    m = rx.search(text)
                    if m:
                        line_no = text.count("\n", 0, m.start()) + 1
                        snippet = text.splitlines()[line_no - 1].strip() if text.splitlines() else ""
                        if allow_lines and line_matches_any(snippet, allow_lines):
                            continue
                        hits.append(f"{rel}:{line_no}:{snippet}")
                        break
            else:
                for line_no, line in enumerate(text.splitlines(), 1):
                    if allow_lines and line_matches_any(line, allow_lines):
                        continue
                    if any(rx.search(line) for rx in patterns):
                        hits.append(f"{rel}:{line_no}:{line.strip()}")
                        break
        return hits

    def check_forbidden_path(self, rule: dict[str, Any]) -> None:
        name = rule["name"]
        if not self.selected(name):
            return
        found = [p for p in rule["paths"] if (self.root / p).exists()]
        if found:
            self.fail(name, f"retired path(s) exist: {', '.join(found)}")
        else:
            self.ok(name)

    def check_required_path(self, rule: dict[str, Any]) -> None:
        name = rule["name"]
        if not self.selected(name):
            return
        missing = [p for p in rule["paths"] if not (self.root / p).exists()]
        if missing:
            self.fail(name, f"required path(s) missing: {', '.join(missing)}")
        else:
            self.ok(name)

    def check_scan(self, rule: dict[str, Any]) -> None:
        name = rule["name"]
        if not self.selected(name):
            return
        hits = self.scan_hits(rule)
        if hits:
            self.fail(name, f"{len(hits)} violation file(s)")
            for hit in hits[:8]:
                print(f"         {hit}")
        else:
            self.ok(name)

    def check_occurrence_cap(self, rule: dict[str, Any]) -> None:
        name = rule["name"]
        if not self.selected(name):
            return
        paths = rule.get("scan_paths", ["include", "src", "apps"])
        allow_paths = rule.get("allow_list", [])
        skip_comments = rule.get("skip_comments", True)
        rx = re.compile(rule["pattern"], re.MULTILINE)
        file_requires = re.compile(rule["file_requires_pattern"], re.MULTILINE) if rule.get("file_requires_pattern") else None
        count = 0
        for path in iter_files(self.root, paths, rule.get("extensions")):
            rel = relpath(self.root, path)
            if path_matches(rel, allow_paths):
                continue
            text = read_text(path)
            if skip_comments:
                text = strip_comments(text)
            if file_requires and not file_requires.search(text):
                continue
            count += len(rx.findall(text))
        maximum = int(rule["max_count"])
        if count > maximum:
            self.fail(name, f"occurrences={count} > cap={maximum}")
        else:
            self.ok(name, f"occurrences={count} <= cap={maximum}")

    def check_unique_owner(self, rule: dict[str, Any]) -> None:
        name = rule["name"]
        if not self.selected(name):
            return
        canonical = self.root / rule["canonical_path"]
        if not canonical.exists():
            self.fail(name, f"canonical path missing: {rule['canonical_path']}")
            return
        scan_paths = rule.get("scan_paths", ["include", "src", "apps"])
        canonical_rx = re.compile(rule["canonical_pattern"], re.MULTILINE)
        canonical_files = 0
        for path in iter_files(self.root, scan_paths):
            if canonical_rx.search(strip_comments(read_text(path))):
                canonical_files += 1
        legacy_count = 0
        count_mode = rule.get("legacy_count_mode", "files")
        legacy_regexes = [re.compile(p, re.MULTILINE) for p in rule.get("legacy_patterns", [])]
        ignore = rule.get("legacy_ignore_paths", [])
        for path in iter_files(self.root, scan_paths):
            rel = relpath(self.root, path)
            if rel == rule["canonical_path"] or path_matches(rel, ignore):
                continue
            text = strip_comments(read_text(path))
            if count_mode == "occurrences":
                legacy_count += sum(len(rx.findall(text)) for rx in legacy_regexes)
            elif any(rx.search(text) for rx in legacy_regexes):
                legacy_count += 1
        expected = int(rule.get("expected_count", 1))
        cap = int(rule.get("legacy_soft_cap", 0))
        if canonical_files >= expected and legacy_count <= cap:
            self.ok(name, f"canonical_files={canonical_files}, legacy={legacy_count}/{cap}")
        else:
            self.fail(name, f"canonical_files={canonical_files} need>={expected}, legacy={legacy_count} cap={cap}")

    def check_contract(self, rule: dict[str, Any]) -> None:
        name = rule["name"]
        if not self.selected(name):
            return
        if "effect_catalog" in rule:
            catalog = self.root / rule["effect_catalog"]
            registry = self.root / rule["processor_registry"]
            if not catalog.exists() or not registry.exists():
                self.fail(name, "effect catalog or processor registry missing")
                return
            catalog_params = re.findall(r"CHRONON_EFFECT\(\s*\d+\s*,\s*\w+\s*,\s*(\w+)", read_text(catalog))
            registered = set(re.findall(r"register_effect_processor<\s*(\w+)\s*>", read_text(registry)))
            duplicates = sorted({p for p in catalog_params if catalog_params.count(p) > 1})
            missing = sorted(set(catalog_params) - registered)
            extra = sorted(registered - set(catalog_params))
            if duplicates or missing or extra:
                self.fail(name, f"duplicates={duplicates}, missing={missing}, extra={extra}")
            else:
                self.ok(name, f"{len(catalog_params)} effects")
            return
        path = self.root / rule["file"]
        if not path.exists():
            if rule.get("optional", False):
                self.ok(name, "optional file absent")
            else:
                self.fail(name, f"file missing: {rule['file']}")
            return
        text = read_text(path)
        required = rule.get("required_patterns", [])
        if rule.get("required_pattern"):
            required = [rule["required_pattern"]]
        forbidden = rule.get("forbidden_patterns", [])
        missing = [p for p in required if not re.search(p, text, re.MULTILINE | re.DOTALL)]
        present = [p for p in forbidden if re.search(p, text, re.MULTILINE | re.DOTALL)]
        if missing or present:
            detail = []
            if missing:
                detail.append(f"missing={missing}")
            if present:
                detail.append(f"forbidden={present}")
            self.fail(name, "; ".join(detail))
        else:
            self.ok(name)

    def check_presence(self, rule: dict[str, Any]) -> None:
        name = rule["name"]
        if not self.selected(name):
            return
        rx = re.compile(rule["pattern"], re.MULTILINE)
        count = 0
        for path in iter_files(self.root, rule["scan_paths"], rule.get("extensions")):
            text = strip_comments(read_text(path)) if rule.get("skip_comments", True) else read_text(path)
            count += len(rx.findall(text))
        minimum = int(rule.get("min_matches", 1))
        if count < minimum:
            self.fail(name, f"matches={count} < minimum={minimum}")
        else:
            self.ok(name, f"matches={count}")

    def check_loc_bound(self, rule: dict[str, Any]) -> None:
        name = rule["name"]
        if not self.selected(name):
            return
        path = self.root / rule["file"]
        if not path.exists():
            self.fail(name, f"file missing: {rule['file']}")
            return
        lines = read_text(path).splitlines()
        if "max_loc" in rule and len(lines) > int(rule["max_loc"]):
            self.fail(name, f"LOC={len(lines)} > {rule['max_loc']}")
            return
        if "max_non_local_includes" in rule:
            count = sum(bool(re.match(r"\s*#include\s*<(?:chronon3d|backends)/", line)) for line in lines)
            if count > int(rule["max_non_local_includes"]):
                self.fail(name, f"non-local includes={count} > {rule['max_non_local_includes']}")
                return
        self.ok(name)

    def check_effect_processor_coverage(self) -> None:
        cfg = self.rules.get("effect_processor_coverage")
        if not cfg or not self.selected(cfg.get("name", "effect_processor_coverage")):
            return
        name = cfg.get("name", "effect_processor_coverage")
        catalog = self.root / cfg["effect_catalog"]
        registry = self.root / cfg["processor_registry"]
        if not catalog.exists() or not registry.exists():
            self.fail(name, "catalog or registry missing")
            return
        catalog_params = re.findall(r"CHRONON_EFFECT\(\s*\d+\s*,\s*\w+\s*,\s*(\w+)", read_text(catalog))
        registered = set(re.findall(r"register_effect_processor<\s*(\w+)\s*>", read_text(registry)))
        duplicates = sorted({p for p in catalog_params if catalog_params.count(p) > 1})
        missing = sorted(set(catalog_params) - registered)
        extra = sorted(registered - set(catalog_params))
        if duplicates or missing or extra:
            self.fail(name, f"duplicates={duplicates}, missing={missing}, extra={extra}")
        else:
            self.ok(name, f"{len(catalog_params)} effects")

    @staticmethod
    def cmake_commands(text: str) -> Iterable[tuple[str, list[str]]]:
        text = re.sub(r"#[^\n]*", "", text)
        command_re = re.compile(r"\b(add_library|add_executable|target_sources)\s*\(", re.I)
        for match in command_re.finditer(text):
            depth, i = 1, match.end()
            while i < len(text) and depth:
                depth += (text[i] == "(") - (text[i] == ")")
                i += 1
            if depth:
                continue
            raw = text[match.end():i - 1]
            toks = [a or b for a, b in re.findall(r'"([^"]+)"|([^\s]+)', raw)]
            if toks:
                yield match.group(1).lower(), toks

    def check_cmake_ownership(self) -> None:
        cfg = self.rules.get("cmake_ownership")
        name = "cmake_ownership"
        if not cfg or not self.selected(name):
            return
        ownership: dict[str, set[str]] = defaultdict(set)
        keywords = {"STATIC", "SHARED", "MODULE", "OBJECT", "INTERFACE", "IMPORTED",
                    "ALIAS", "WIN32", "MACOSX_BUNDLE", "EXCLUDE_FROM_ALL",
                    "PRIVATE", "PUBLIC"}
        for cmake in pruned_rglob(self.root, "CMakeLists.txt"):
            if any(part in EXCLUDED_DIRS or part.startswith("build-") for part in cmake.parts):
                continue
            for _, args in self.cmake_commands(read_text(cmake)):
                target = args[0]
                for raw in args[1:]:
                    if raw.upper() in keywords or "$<" in raw or "${" in raw:
                        continue
                    if not raw.lower().endswith((".cpp", ".cc", ".cxx")):
                        continue
                    candidate = Path(raw)
                    if not candidate.is_absolute():
                        candidate = (cmake.parent / candidate).resolve()
                    if candidate.is_file():
                        ownership[relpath(self.root, candidate)].add(target)
        allowed = {e["path"]: set(e["owners"]) for e in cfg.get("allowed_multi_owner", [])}
        conflicts = [(p, sorted(t)) for p, t in ownership.items()
                     if len(t) > 1 and allowed.get(p) != set(t)]
        if conflicts:
            self.fail(name, f"{len(conflicts)} multi-owner source(s)")
            for p, t in conflicts[:10]:
                print(f"         {p} -> {', '.join(t)}")
        else:
            self.ok(name, f"{len(ownership)} concrete source entries")

    def check_test_registration(self) -> None:
        cfg = self.rules.get("test_registration")
        if not cfg:
            return
        name = cfg.get("name", "test_registration")
        if not self.selected(name):
            return
        hits: list[str] = []
        for path in list((self.root / "tests").rglob("*.cmake")) + list((self.root / "tests").rglob("CMakeLists.txt")):
            text = re.sub(r"#[^\n]*", "", read_text(path))
            for m in re.finditer(r"^\s*add_executable\(\s*chronon3d_.*?_tests?\b", text, re.MULTILINE):
                line = text.count("\n", 0, m.start()) + 1
                hits.append(f"{relpath(self.root, path)}:{line}")
        if hits:
            self.fail(name, f"{len(hits)} raw test executable registration(s)")
            for hit in hits[:10]:
                print(f"         {hit}")
        else:
            self.ok(name, "recursive tests/**/*.cmake + CMakeLists.txt clean")

    @staticmethod
    def parse_set_block(text: str, var: str) -> list[str]:
        match = re.search(rf"set\s*\(\s*{re.escape(var)}\b(.*?)\n\s*\)", text, re.DOTALL)
        if not match:
            return []
        body = re.sub(r"#[^\n]*", "", match.group(1))
        return re.findall(r"\b[A-Za-z_][A-Za-z0-9_:+.-]*\b", body)

    def check_cmake_registry(self) -> None:
        cfg = self.rules.get("cmake_registry")
        if not cfg:
            return
        name = cfg.get("name", "cmake_registry")
        if not self.selected(name):
            return
        declared: set[str] = set()
        for cmake in (self.root / "src").rglob("CMakeLists.txt"):
            text = re.sub(r"#[^\n]*", "", read_text(cmake))
            declared.update(re.findall(
                r"add_library\(\s*([A-Za-z_][A-Za-z0-9_]*)\s+(?:OBJECT|INTERFACE)\b", text))
        registry = read_text(self.root / cfg["registry_file"])
        listed = set(self.parse_set_block(registry, "CHRONON3D_REGISTRY_OBJECT_LIBS"))
        listed.update(self.parse_set_block(registry, "CHRONON3D_REGISTRY_INTERFACE_LIBS"))
        missing = sorted(declared - listed)
        if missing:
            self.fail(name, f"unregistered OBJECT/INTERFACE libraries: {missing}")
        else:
            self.ok(name, f"{len(declared)} libraries registered")

    def check_vcpkg_parity(self) -> None:
        cfg = self.rules.get("vcpkg_parity")
        if not cfg:
            return
        name = cfg.get("name", "vcpkg_parity")
        if not self.selected(name):
            return
        packages: set[str] = set()
        cmake_files = cfg.get("cmake_files", ["CMakeLists.txt"])
        for rel in cmake_files:
            cmake = self.root / rel
            if cmake.exists():
                packages.update(re.findall(r"find_package\(\s*([A-Za-z_][A-Za-z0-9_-]*)", read_text(cmake)))
        manifest = json.loads(read_text(self.root / cfg["manifest"]))
        deps: set[str] = set()
        def add_dep(entry: Any) -> None:
            if isinstance(entry, str):
                deps.add(entry.lower())
            elif isinstance(entry, dict) and "name" in entry:
                deps.add(str(entry["name"]).lower())
        for dep in manifest.get("dependencies", []):
            add_dep(dep)
        for feature in manifest.get("features", {}).values():
            for dep in feature.get("dependencies", []):
                add_dep(dep)
        mapping = {k.lower(): v.lower() for k, v in cfg.get("package_map", {}).items()}
        allow = {x.lower() for x in cfg.get("system_allow", [])}
        missing = []
        for package in sorted(packages):
            key = package.lower()
            if key in allow:
                continue
            dep = mapping.get(key, key)
            if dep not in deps:
                missing.append(f"{package}->{dep}")
        if missing:
            self.fail(name, f"find_package without manifest dependency: {missing}")
        else:
            self.ok(name, f"{len(packages)} package names checked")

    def check_sdk_public_deps(self) -> None:
        cfg = self.rules.get("sdk_public_deps")
        if not cfg:
            return
        name = cfg.get("name", "sdk_public_deps")
        if not self.selected(name):
            return
        registry = read_text(self.root / cfg["registry_file"])
        deps = self.parse_set_block(registry, "CHRONON3D_SDK_PUBLIC_DEPS")
        template = read_text(self.root / cfg["template_file"])
        substitutions = template.count("@CHRONON3D_FIND_DEPENDENCY_LINES@")
        marker_finds = 0
        in_marker = False
        for line in template.splitlines():
            if "AUTO-GENERATED FROM CHRONON3D_SDK_PUBLIC_DEPS" in line:
                in_marker = True
                continue
            if "END AUTO-GENERATED BLOCK" in line:
                in_marker = False
            elif in_marker and re.match(r"\s*find_dependency\(", line):
                marker_finds += 1
        if not deps or substitutions != 1 or marker_finds:
            self.fail(name, f"deps={len(deps)}, substitutions={substitutions}, handwritten={marker_finds}")
        else:
            self.ok(name, f"{len(deps)} public deps, one generated marker")

    def check_public_include_resolution(self) -> None:
        cfg = self.rules.get("public_include_resolution")
        if not cfg:
            return
        name = cfg.get("name", "public_include_resolution")
        if not self.selected(name):
            return
        hits: list[str] = []
        rx = re.compile(r'#include\s*<chronon3d/([^>]+)>')
        for path in iter_files(self.root, cfg.get("scan_paths", ["include", "src", "tests", "apps"])):
            text = strip_comments(read_text(path))
            for m in rx.finditer(text):
                inc = m.group(1)
                if (self.root / "src" / inc).is_file() and not (self.root / "include/chronon3d" / inc).is_file():
                    line = text.count("\n", 0, m.start()) + 1
                    hits.append(f"{relpath(self.root, path)}:{line}:chronon3d/{inc}")
        if hits:
            self.fail(name, f"{len(hits)} src-only headers exposed via public include")
            for hit in hits[:10]:
                print(f"         {hit}")
        else:
            self.ok(name)

    def check_gitignored(self) -> None:
        cfg = self.rules.get("gitignored")
        name = "gitignored"
        if not cfg or not self.selected(name):
            return
        violations: list[str] = []

        def tracked(prefix: str) -> list[str]:
            try:
                out = subprocess.run(
                    ["git", "ls-files", prefix], cwd=self.root,
                    text=True, capture_output=True, timeout=20,
                ).stdout
                return [x for x in out.splitlines() if x]
            except (OSError, subprocess.TimeoutExpired):
                return []

        for directory in cfg.get("dirs", []):
            if tracked(f"{directory}/"):
                violations.append(directory)

        for pattern in cfg.get("build_globs", []):
            for path in self.root.glob(pattern):
                if path.is_dir() and tracked(f"{relpath(self.root, path)}/"):
                    violations.append(relpath(self.root, path))

        for pattern in cfg.get("tmp_gate_globs", []):
            for path in self.root.glob(pattern):
                if path.is_dir() and tracked(f"{relpath(self.root, path)}/"):
                    violations.append(relpath(self.root, path))

        for pattern in cfg.get("file_patterns", []):
            clean = pattern.lstrip("/")
            for path in self.root.glob(clean):
                if path.is_file() and tracked(relpath(self.root, path)):
                    violations.append(relpath(self.root, path))

        abs_pattern = cfg.get("absolute_path_pattern")
        if abs_pattern:
            try:
                proc = subprocess.run(
                    ["git", "grep", "-l", "--cached", "-E", "--", abs_pattern],
                    cwd=self.root, text=True, capture_output=True, timeout=30,
                )
                violations.extend(x for x in proc.stdout.splitlines() if x)
            except (OSError, subprocess.TimeoutExpired):
                pass

        if violations:
            self.fail(name, f"{len(set(violations))} ignored/tracked violation(s)")
            for item in sorted(set(violations))[:10]:
                print(f"         {item}")
        else:
            self.ok(name)

    def run(self) -> int:
        for rule in self.rules.get("forbidden_path", []):
            self.check_forbidden_path(rule)
        for rule in self.rules.get("required_path", []):
            self.check_required_path(rule)
        for key in ("forbidden_symbol", "forbidden_include", "boundary", "scan"):
            for rule in self.rules.get(key, []):
                self.check_scan(rule)
        for rule in self.rules.get("occurrence_cap", []):
            self.check_occurrence_cap(rule)
        for rule in self.rules.get("unique_source_owner", []):
            self.check_unique_owner(rule)
        for rule in self.rules.get("contract", []):
            self.check_contract(rule)
        for rule in self.rules.get("presence", []):
            self.check_presence(rule)
        for rule in self.rules.get("loc_bound", []):
            self.check_loc_bound(rule)
        self.check_effect_processor_coverage()
        self.check_cmake_ownership()
        self.check_test_registration()
        self.check_cmake_registry()
        self.check_vcpkg_parity()
        self.check_sdk_public_deps()
        self.check_public_include_resolution()
        self.check_gitignored()
        total = len(self.passes) + len(self.failures)
        if self.failures:
            print(f"GATE_FAIL: {len(self.failures)}/{total} architecture rule(s) FAILED")
            return 1
        print(f"GATE_PASS: {len(self.passes)}/{total} architecture rule(s) PASSED")
        return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Canonical declarative architecture gate")
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--rules", type=Path)
    parser.add_argument("--rule-prefix")
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()
    root = args.root.resolve()
    rules_path = args.rules.resolve() if args.rules else root / "tools/architecture_rules.toml"
    if not rules_path.exists():
        print(f"GATE_FAIL_INTERNAL: rules file not found: {rules_path}", file=sys.stderr)
        return 2
    if args.list:
        rules = load_registry(rules_path)
        for key, value in rules.items():
            if isinstance(value, list):
                for item in value:
                    if isinstance(item, dict) and "name" in item:
                        print(f"{key}:{item['name']}")
            elif isinstance(value, dict) and "name" in value:
                print(f"{key}:{value['name']}")
        return 0
    return Runner(root, rules_path, args.rule_prefix).run()


if __name__ == "__main__":
    sys.exit(main())
