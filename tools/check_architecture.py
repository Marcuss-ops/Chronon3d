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
        self.configured_build_dir: Path | None = None

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

    def check_doc_tool_reference(self, rule: dict[str, Any]) -> None:
        """Operative docs must not point at tools/ paths that do not exist.

        A runbook command that fails on invocation is worse than a missing
        command: the reader believes the certification happened.  This gate
        exists because `docs/GOVERNANCE.md` prescribed a gate script that had
        already been consolidated away, so the weekly ritual certified nothing
        while looking like it did.  Dated audit records and forward-looking
        plans legitimately name paths as they were or will be, so the rule
        excludes them by path pattern.
        """
        name = rule["name"]
        if not self.selected(name):
            return
        rx = re.compile(rule["pattern"], re.MULTILINE)
        exclude = rule.get("exclude_paths", [])
        allowed = set(rule.get("allow_missing", []))
        references = 0
        missing: dict[str, set[str]] = {}
        for path in iter_files(self.root, rule["doc_paths"], rule.get("extensions", [".md"])):
            rel = relpath(self.root, path)
            if path_matches(rel, exclude):
                continue
            for match in rx.finditer(read_text(path)):
                ref = match.group(0).rstrip(".,;:)]}`\"'")
                if "." not in Path(ref).name:
                    continue  # directory references are out of scope
                references += 1
                if ref in allowed or (self.root / ref).exists():
                    continue
                missing.setdefault(ref, set()).add(rel)
        if missing:
            self.fail(name, f"{len(missing)} reference(s) to nonexistent tools/ paths")
            for ref, where in sorted(missing.items())[:8]:
                print(f"         {ref}  <- {', '.join(sorted(where))}")
        else:
            self.ok(name, f"{references} tool reference(s) resolved")

    def check_scan_path_existence(self) -> None:
        """Every declared scan_path must resolve (or be a declared exception).

        A rule whose scan_path no longer exists can never fail, so its silence
        is indistinguishable from a pass — the gate looks enforced while
        enforcing nothing. That is how `render_session_include_boundary` kept
        "guarding" a header that had already been deleted. Paths that are
        legitimately absent from a core checkout (external content packs) are
        listed in the registry's `[scan_path_policy] optional_globs`.

        The same hazard applies to the FILE a rule pins (contract / loc_bound):
        a rule that cannot find its file enforces nothing. Live example: the
        native video decoder TU was split into `_session`/`_decode`/`_helpers`
        and two contracts kept pointing at the retired monolith, so the decoder
        cache and concurrency invariants were enforced by zero code while the
        gate reported PASS (audit P0-1). Validation lives here — not per rule —
        so it also holds under `--rule-prefix` and for every registry fragment.
        """
        policy = self.rules.get("scan_path_policy") or {}
        name = policy.get("name", "scan_path_existence")
        if not self.selected(name):
            return
        optional = list(policy.get("optional_globs", []))
        missing: dict[str, set[str]] = {}
        for kind in (
            "forbidden_symbol", "forbidden_include", "boundary", "scan",
            "occurrence_cap", "presence", "unique_source_owner",
        ):
            for rule in self.rules.get(kind, []):
                for scan_path in rule.get("scan_paths") or []:
                    if (self.root / scan_path).exists():
                        continue
                    if any(fnmatch.fnmatch(scan_path, glob) for glob in optional):
                        continue
                    missing.setdefault(scan_path, set()).add(rule.get("name", "?"))
        for kind in ("contract", "loc_bound"):
            for rule in self.rules.get(kind, []):
                target = rule.get("file")
                if not target or (self.root / target).exists():
                    continue
                # Declared absence is allowed, but only when it is declared:
                # `optional` plus a human-readable reason. `optional` alone used
                # to be silently green (audit P0-1).
                if rule.get("optional") and str(rule.get("optional_reason", "")).strip():
                    continue
                if any(fnmatch.fnmatch(target, glob) for glob in optional):
                    continue
                missing.setdefault(target, set()).add(rule.get("name", "?"))
        if missing:
            self.fail(name, f"{len(missing)} declared path(s) do not resolve")
            for scan_path, owners in sorted(missing.items())[:12]:
                print(f"         {scan_path}  <- {', '.join(sorted(owners))}")
        else:
            self.ok(name)

    @staticmethod
    def _normalize_legacy_type(qualified: str) -> str:
        """Reduce a kLegacyErrorMappings type to its adapter-rule stem form.

        Namespace components are kept: the adapter rules name the type as the
        programmer sees it, and a namespace can be the only thing distinguishing
        two rows.

        `chronon3d::sdk::RenderErrorCode`            → `sdk_render_error`
        `chronon3d::runtime::job::RenderJobRequestErrorCode`
                                                     → `runtime_job_render_job_request_error`
        `chronon3d::runtime::PreparationError::Code` → `runtime_preparation_error`
        """
        parts = [p for p in qualified.split("::") if p]
        if parts and parts[0] == "chronon3d":
            parts = parts[1:]
        # A nested `::Code`/`::Kind` qualifier names the enum inside a struct:
        # the struct is the identity that matters.
        if len(parts) > 1 and parts[-1] in {"Code", "Kind", "Type", "Reason"}:
            parts = parts[:-1]
        if not parts:
            return ""
        parts[-1] = re.sub(
            r"(?<!^)(?=[A-Z])", "_", parts[-1])
        joined = "_".join(parts).lower()
        return re.sub(r"_(code|kind|reason|policy|path|state|pressure)$", "", joined)

    def check_error_adapter_inventory(self) -> None:
        """The migration gate must project the inventory, not duplicate it.

        AUDIT F18 — the legacy error inventory lives in
        include/chronon3d/core/types/error.hpp (kLegacyErrorMappings), while each
        `<stem>_error_construction_adapter_only` scan rule repeats one row of it.
        Nothing tied the two together, so a rule could keep guarding a type that
        had already been deleted from the tree — enforcement that cannot fail,
        the same defect class as the phantom scan paths. The header is the
        authority for *which* adapter guards may exist; this check requires every
        adapter-rule stem to resolve to a registered legacy type, and pins the
        declared array arity to the number of rows actually present.
        """
        cfg = self.rules.get("error_adapter_inventory") or {}
        name = cfg.get("name", "error_adapter_inventory_coverage")
        if not self.selected(name):
            return
        header_rel = cfg.get(
            "header", "include/chronon3d/core/types/error.hpp")
        header = self.root / header_rel
        if not header.exists():
            if not cfg:
                # Not configured AND the inventory is absent from this tree: the
                # registry under test (a fragment, or a selftest mirror tree)
                # does not carry the error-inventory contract. Apart from the
                # crash this was the reason the gate selftest could not observe
                # its own cache-taxonomy assertions. The root manifest DOES
                # declare this section, so on a real checkout the rule stays
                # enforced — including the case where the config is present and
                # someone deletes the header.
                return
            self.fail(name, f"canonical error header missing: {header_rel}")
            return
        text = strip_comments(read_text(header))
        declared = re.search(
            r"std::array<LegacyErrorMapping,\s*(\d+)>\s*kLegacyErrorMappings\{\{(.*?)\}\};",
            text, re.DOTALL)
        if not declared:
            self.fail(name, "kLegacyErrorMappings block not found")
            return
        arity = int(declared.group(1))
        rows = re.findall(r'\{\s*"([^"]+)"\s*,\s*"([^"]+)"\s*\}', declared.group(2))
        stems = {self._normalize_legacy_type(qualified) for qualified, _ in rows}
        # Adapter guards for types deliberately outside the inventory (typed
        # Result payloads whose construction is already owned, not `from_legacy`
        # registrations). Declared explicitly so the exemption stays auditable.
        foreign = {
            self._normalize_legacy_type(str(t))
            for t in cfg.get("foreign_adapter_types", [])
        }

        # The error-adapter family is `<subject>_error_construction_adapter_only`;
        # the bare `_construction_adapter_only` suffix also carries unrelated
        # guards (e.g. conversion_result), so select on the full family name and
        # slice only the shared tail.
        family = "_error_construction_adapter_only"
        suffix = "_construction_adapter_only"
        stems_by_rule = {
            rule["name"][: -len(suffix)]: rule["name"]
            for rule in self.rules.get("scan", [])
            if str(rule.get("name", "")).endswith(family)
        }

        def related(stem: str, candidate: str) -> bool:
            """True when one underscore-stem is a prefix/suffix-extension of the
            other. The rule stems and the normalized inventory names differ only
            in how much namespace they carry, so containment either way is the
            honest test."""
            return (stem == candidate or stem.startswith(candidate + "_") or
                    candidate.startswith(stem + "_") or
                    stem.endswith("_" + candidate) or
                    candidate.endswith("_" + stem))

        def resolves(stem: str) -> bool:
            if any(related(stem, extra) for extra in foreign):
                return True
            return any(related(stem, known) for known in stems)

        unmatched = sorted(
            rule_name for stem, rule_name in stems_by_rule.items()
            if not resolves(stem))
        problems: list[str] = []
        if arity != len(rows):
            problems.append(
                f"declared arity {arity} != {len(rows)} inventory row(s)")
        if unmatched:
            problems.append(
                f"adapter rule(s) with no registered legacy type: {unmatched}")
        if not stems_by_rule:
            problems.append("no adapter-construction rules loaded")
        if problems:
            self.fail(name, "; ".join(problems))
        else:
            self.ok(name, f"{len(rows)} inventory row(s) ↔ {len(stems_by_rule)} adapter rule(s)")

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
            # A rule that targets a file nobody can find enforces NOTHING, so
            # its absence must be *declared*, not inferred from `optional`. The
            # old `optional = true` branch printed PASS with the detail
            # "optional file absent", which is how the two native-decoder
            # contracts went silent: the decoder TU was split and the rules kept
            # reporting green (audit P0-1). Absence now requires an explicit
            # `optional_reason`, and the reason is surfaced in the output so a
            # stale target is visible instead of authoritative.
            reason = str(rule.get("optional_reason", "")).strip()
            if reason and rule.get("optional", False):
                self.ok(name, f"target absent on purpose — {reason}")
            else:
                self.fail(
                    name,
                    f"file missing: {rule['file']} — a contract that cannot "
                    "find its file enforces nothing; repoint it at the TU that "
                    "owns the invariant, or declare `optional = true` with an "
                    "`optional_reason`",
                )
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

    def cmake_cmake_files(self):
        """Yield every CMake build file (CMakeLists.txt and *.cmake) under the
        root, excluding build trees, newest first is irrelevant — pruned walk."""
        seen: set[Path] = set()
        for pattern in ("CMakeLists.txt", "*.cmake"):
            for path in pruned_rglob(self.root, pattern):
                if any(part in EXCLUDED_DIRS or part.startswith("build-")
                       for part in path.parts):
                    continue
                if path not in seen:
                    seen.add(path)
                    yield path

    def check_cmake_dag_rule(self, rule: dict[str, Any]) -> None:
        """Phase 11/12 CMake DAG gates, read from the same declarative registry.

        Two rule kinds are supported:
          * forbidden_target — a demolished target name must not be (re)created
            by add_library/add_executable (plain or ALIAS); mentioning it in a
            comment or documentation is fine, defining it is a gate failure.
          * forbidden_edge — a source target must not list the forbidden
            dependencies in target_link_libraries (leaf -> whole-aggregate
            inverted dependencies and facade fallbacks).
        This is a source-of-truth check over the CMakeLists/*.cmake files; CI
        re-runs the same invariants on the configured codemodel.
        """
        name = rule["name"]
        if not self.selected(name):
            return
        kind = rule.get("kind")
        violations: list[str] = []
        for path in self.cmake_cmake_files():
            rel = relpath(self.root, path)
            text = read_text(path)
            # CMake comments are '#...'; keep string literals intact.
            text = re.sub(r'"(?:\\.|[^"\\])*"|#[^\n]*',
                          lambda m: m.group(0) if m.group(0).startswith('"') else re.sub(r"[^\n]", " ", m.group(0)),
                          text)
            if kind == "forbidden_target":
                for target in rule.get("targets", []):
                    rx = re.compile(
                        r"\b(?:add_library|add_executable)\s*\(\s*" + re.escape(target) + r"(?:\s+ALIAS)?\b",
                        re.I)
                    for m in rx.finditer(text):
                        line_no = text.count("\n", 0, m.start()) + 1
                        violations.append(f"{rel}:{line_no}: defines demolished target {target}")
            elif kind == "forbidden_edge":
                source = rule.get("source")
                forbidden = rule.get("forbidden_deps", [])
                command_re = re.compile(r"\btarget_link_libraries\s*\(", re.I)
                for match in command_re.finditer(text):
                    depth, i = 1, match.end()
                    while i < len(text) and depth:
                        depth += (text[i] == "(") - (text[i] == ")")
                        i += 1
                    if depth:
                        continue
                    raw = text[match.end():i - 1]
                    toks = [a or b for a, b in re.findall(r'"([^"]+)"|([^\s]+)', raw)]
                    if toks and toks[0] == source:
                        for dep in forbidden:
                            if dep in toks:
                                line_no = text.count("\n", 0, match.start()) + 1
                                violations.append(
                                    f"{rel}:{line_no}: {source} links {dep} (forbidden edge)")
            else:
                self.fail(name, f"unknown cmake_dag kind: {kind}")
                return
        if violations:
            self.fail(name, f"{len(violations)} violation(s)")
            for item in violations[:10]:
                print(f"         {item}")
        else:
            self.ok(name, f"clean ({len(list(self.cmake_cmake_files()))} cmake files)")

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
        registry_rel = cfg["registry_file"]
        if not (self.root / registry_rel).exists():
            # A crash would truncate the whole report: every rule after this one
            # would go unverified while the process still exits non-zero. That
            # is how the selftest's newest assertions (cache taxonomy) stopped
            # observing anything without anyone noticing. Name the missing file.
            self.fail(name, f"registry file missing: {registry_rel}")
            return
        registry = read_text(self.root / registry_rel)
        listed = set(self.parse_set_block(registry, "CHRONON3D_REGISTRY_OBJECT_LIBS"))
        listed.update(self.parse_set_block(registry, "CHRONON3D_REGISTRY_INTERFACE_LIBS"))
        missing = sorted(declared - listed)
        if missing:
            self.fail(name, f"unregistered OBJECT/INTERFACE libraries: {missing}")
        else:
            self.ok(name, f"{len(declared)} libraries registered")

    def check_configured_cmake(self, build_dir: Path) -> None:
        """Check the generated CMake target graph, not only source text.

        Source-level CMake checks cannot see generator expressions, platform
        branches, or targets assembled by included files.  CMake's File API
        codemodel is the authoritative configured graph for those cases.  A
        build directory is intentionally opt-in: the fast static gate can run
        from a checkout, while CI/build validation passes the configured
        directory and turns missing codemodel data into a hard failure.
        """
        name = "configured_cmake_dag"
        if not self.selected(name):
            return
        reply = build_dir / ".cmake" / "api" / "v1" / "reply"
        indexes = sorted(reply.glob("index-*.json"))
        if not indexes:
            self.fail(name, f"CMake File API reply missing in {reply}")
            return
        try:
            index = json.loads(indexes[-1].read_text(encoding="utf-8"))
            codemodel_object = next(
                obj for obj in index.get("objects", [])
                if obj.get("kind") == "codemodel"
            )
            model_path = reply / codemodel_object["jsonFile"]
            model = json.loads(model_path.read_text(encoding="utf-8"))
            configuration = model["configurations"][0]
        except (OSError, KeyError, StopIteration, IndexError, TypeError, json.JSONDecodeError) as error:
            self.fail(name, f"invalid CMake codemodel: {error}")
            return

        targets = configuration.get("targets", [])
        by_id = {target.get("id"): target for target in targets}
        by_name = {target.get("name"): target for target in targets}
        violations: list[str] = []
        target_details: dict[str, dict[str, Any]] = {}

        for rule in self.rules.get("cmake_dag", []):
            if rule.get("kind") == "forbidden_target":
                for target_name in rule.get("targets", []):
                    if target_name in by_name:
                        violations.append(
                            f"target {target_name!r} is present in configured codemodel")
            elif rule.get("kind") == "forbidden_edge":
                source = rule.get("source")
                target = by_name.get(source)
                if not target:
                    continue
                dependency_names = {
                    by_id[dep["id"]].get("name")
                    for dep in target.get("dependencies", [])
                    if dep.get("id") in by_id
                }
                for dependency in rule.get("forbidden_deps", []):
                    if dependency in dependency_names:
                        violations.append(
                            f"configured target {source} links {dependency}")

        for rule in self.rules.get("configured_target_source", []):
            target_name = rule["target"]
            target = by_name.get(target_name)
            if not target:
                self.fail(name, f"configured target missing: {target_name}")
                continue
            try:
                detail_path = reply / target["jsonFile"]
                target_details[target_name] = json.loads(
                    detail_path.read_text(encoding="utf-8"))
            except (OSError, KeyError, TypeError, json.JSONDecodeError) as error:
                self.fail(name, f"invalid target codemodel for {target_name}: {error}")
                continue
            source_paths = [
                source.get("path", "")
                for source in target_details[target_name].get("sources", [])
                if source.get("path")
            ]
            for pattern in rule.get("forbidden_patterns", []):
                matcher = re.compile(pattern)
                for source_path in source_paths:
                    if matcher.search(source_path):
                        violations.append(
                            f"configured target {target_name} owns forbidden source {source_path}")

        if violations:
            self.fail(name, f"{len(violations)} configured violation(s)")
            for violation in violations[:10]:
                print(f"         {violation}")
        else:
            self.ok(name, f"clean ({len(by_name)} configured targets)")

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
        manifest_rel = cfg["manifest"]
        if not (self.root / manifest_rel).exists():
            self.fail(name, f"manifest missing: {manifest_rel}")
            return
        manifest = json.loads(read_text(self.root / manifest_rel))
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
        registry_rel = cfg["registry_file"]
        template_rel = cfg["template_file"]
        if not (self.root / registry_rel).exists():
            self.fail(name, f"registry file missing: {registry_rel}")
            return
        if not (self.root / template_rel).exists():
            self.fail(name, f"template missing: {template_rel}")
            return
        registry = read_text(self.root / registry_rel)
        deps = self.parse_set_block(registry, "CHRONON3D_SDK_PUBLIC_DEPS")
        template = read_text(self.root / template_rel)
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

    def check_duplicate_basenames(self, rule: dict[str, Any]) -> None:
        """Reject new source basename collisions while grandfathering history.

        A basename collision is often harmless across public/source/test
        boundaries, but it makes namespace and CMake ownership drift easy to
        reintroduce. The allowlist is deliberately name-only and checked in;
        adding a new collision therefore requires an explicit architecture
        review instead of silently growing the duplicate set.
        """
        name = rule["name"]
        if not self.selected(name):
            return
        allowlist_path = self.root / rule["allowlist"]
        if not allowlist_path.exists():
            self.fail(name, f"allowlist missing: {rule['allowlist']}")
            return
        grandfathered = {
            line.strip() for line in read_text(allowlist_path).splitlines()
            if line.strip() and not line.lstrip().startswith("#")
        }
        by_basename: dict[str, list[str]] = defaultdict(list)
        for path in iter_files(self.root, rule.get("scan_paths", []), rule.get("extensions")):
            by_basename[path.name].append(relpath(self.root, path))
        duplicates = {
            basename: sorted(paths)
            for basename, paths in by_basename.items()
            if len(paths) > 1
        }
        unreviewed = sorted(set(duplicates) - grandfathered)
        if unreviewed:
            self.fail(name, f"new duplicate basename(s): {unreviewed}")
            for basename in unreviewed[:10]:
                print(f"         {basename}: {', '.join(duplicates[basename])}")
            return
        self.ok(name, f"{len(duplicates)} duplicate basename(s), all grandfathered")

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

    def guard(self, label: str, fn) -> None:
        """Run one rule; an unexpected exception becomes a named FAIL.

        A gate that aborts mid-run is worse than a red one: every rule after the
        crash is silently unverified. A missing auxiliary file (registry,
        manifest, template) used to raise out of `read_text` and truncate the
        report — which is exactly how the selftest's cache-taxonomy assertions
        stopped observing anything while the run still exited non-zero. The exit
        code is unchanged (still a failure) but now it is attributed, and the
        remaining rules keep running.
        """
        try:
            fn()
        except Exception as exc:  # noqa: BLE001 — the gate must not abort
            self.fail(label, f"gate internal error: {type(exc).__name__}: {exc}")

    def run(self) -> int:
        self.guard("scan_path_existence", self.check_scan_path_existence)
        self.guard("error_adapter_inventory_coverage", self.check_error_adapter_inventory)
        for rule in self.rules.get("forbidden_path", []):
            name = f"forbidden_path:{rule.get('name', '?')}"
            self.guard(name, lambda rule=rule: self.check_forbidden_path(rule))
        for rule in self.rules.get("required_path", []):
            name = f"required_path:{rule.get('name', '?')}"
            self.guard(name, lambda rule=rule: self.check_required_path(rule))
        for rule in self.rules.get("doc_tool_reference", []):
            name = f"doc_tool_reference:{rule.get('name', '?')}"
            self.guard(name, lambda rule=rule: self.check_doc_tool_reference(rule))
        for key in ("forbidden_symbol", "forbidden_include", "boundary", "scan"):
            for rule in self.rules.get(key, []):
                name = f"{key}:{rule.get('name', '?')}"
                self.guard(name, lambda rule=rule: self.check_scan(rule))
        for rule in self.rules.get("occurrence_cap", []):
            name = f"occurrence_cap:{rule.get('name', '?')}"
            self.guard(name, lambda rule=rule: self.check_occurrence_cap(rule))
        for rule in self.rules.get("unique_source_owner", []):
            name = f"unique_source_owner:{rule.get('name', '?')}"
            self.guard(name, lambda rule=rule: self.check_unique_owner(rule))
        for rule in self.rules.get("contract", []):
            name = f"contract:{rule.get('name', '?')}"
            self.guard(name, lambda rule=rule: self.check_contract(rule))
        for rule in self.rules.get("presence", []):
            name = f"presence:{rule.get('name', '?')}"
            self.guard(name, lambda rule=rule: self.check_presence(rule))
        for rule in self.rules.get("loc_bound", []):
            name = f"loc_bound:{rule.get('name', '?')}"
            self.guard(name, lambda rule=rule: self.check_loc_bound(rule))
        self.guard("effect_processor_coverage", self.check_effect_processor_coverage)
        for rule in self.rules.get("cmake_dag", []):
            name = f"cmake_dag:{rule.get('name', '?')}"
            self.guard(name, lambda rule=rule: self.check_cmake_dag_rule(rule))
        self.guard("cmake_ownership", self.check_cmake_ownership)
        self.guard("test_registration", self.check_test_registration)
        self.guard("cmake_registry", self.check_cmake_registry)
        if self.configured_build_dir is not None:
            self.guard(
                "configured_cmake",
                lambda: self.check_configured_cmake(self.configured_build_dir),
            )
        self.guard("vcpkg_parity", self.check_vcpkg_parity)
        self.guard("sdk_public_deps_wiring", self.check_sdk_public_deps)
        self.guard("public_include_resolution", self.check_public_include_resolution)
        for rule in self.rules.get("duplicate_basename", []):
            name = f"duplicate_basename:{rule.get('name', '?')}"
            self.guard(name, lambda rule=rule: self.check_duplicate_basenames(rule))
        self.guard("gitignored", self.check_gitignored)
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
    parser.add_argument(
        "--build-dir",
        type=Path,
        help="check the configured CMake File API codemodel in this build directory",
    )
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
    runner = Runner(root, rules_path, args.rule_prefix)
    runner.configured_build_dir = (
        args.build_dir.resolve() if args.build_dir else None
    )
    return runner.run()


if __name__ == "__main__":
    sys.exit(main())
