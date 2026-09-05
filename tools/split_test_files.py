#!/usr/bin/env python3
"""Split oversized doctest test files into <=600-line parts.

Model
-----
A file parses into an ordered unit stream (stream order == file order):

  prelude     lines before the first construct that owns the first TEST_CASE
              (a test itself or a guard region containing it); repeated
              verbatim in every part.
  test        col-0 TEST_CASE block outside top-level guard regions.
  region      top-level guard region (#if..#endif at depth 0) containing at
              least one whole TEST_CASE. Stored branch-aware:
              pre (includes the #if opener) + branch units + optional
              #else/#elif + branch units + #endif. Chunked across parts;
              every chunk re-wraps pre + branch slices + #endif, which is
              always valid C++ regardless of slice emptiness.
  atomic      any other top-level construct between tests (helper namespace/
              class blocks, includes/usings, test-free guard regions).
              Never split. An atomic unit that precedes at least one test is
              "carried": it is emitted in EVERY part (later tests may need
              its definitions). Units after the last test stay local.

A part renders as: 4-line banner + prelude + carried units + own units.
Helpers are carried by REFERENCE, not blanket: a helper defined in an earlier
part is copied into a later part only if that part's text mentions one of the
helper's defined symbols (transitively through helper-to-helper calls).
Unparseable helpers are conservatively carried everywhere. Validations: every
part <= 600 lines, guard-balanced, and the global TEST_CASE census is
preserved exactly.

Part files: <stem>_partN.cpp next to the original; the original is deleted.
Real (non-dry) runs update the known CMake citation sites.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

MAX_LINES = 600
HEADER_LINES = 4
GUARD_OPEN = re.compile(r"^#(if|ifdef|ifndef)\b")
TEST_CASE_OPEN = re.compile(r"^(TEST_CASE|TEST_CASE_FIXTURE)\s*\(")

MANIFEST_SITES = [
    "tests/core_tests.cmake",
    "tests/scene_tests.cmake",
    "tests/renderer_tests.cmake",
    "tests/animation_timeline_tests.cmake",
    "tests/shader_abi_tests.cmake",
    "tests/text_domain_tests.cmake",
    "tests/manifests/core_general_sources.cmake",
    "tests/manifests/core_text_sources.cmake",
]


def find_test_blocks(lines: list[str]) -> list[tuple[int, int]]:
    """(start, end_inclusive) of every col-0 TEST_CASE block."""
    blocks: list[tuple[int, int]] = []
    i = 0
    n = len(lines)
    while i < n:
        if TEST_CASE_OPEN.match(lines[i]):
            j = i + 1
            while j < n and lines[j] != "}":
                j += 1
            if j >= n:
                raise ValueError(f"TEST_CASE at line {i + 1} never closes at col 0")
            blocks.append((i, j))
            i = j + 1
            continue
        i += 1
    return blocks


def guard_span(lines: list[str], open_idx: int) -> tuple[int, int]:
    """(open, close) for the guard at open_idx, nesting-aware."""
    depth = 0
    for i in range(open_idx, len(lines)):
        line = lines[i]
        if GUARD_OPEN.match(line):
            depth += 1
        elif line.startswith("#endif"):
            depth -= 1
            if depth == 0:
                return open_idx, i
    raise ValueError(f"unterminated guard at line {open_idx + 1}")


def col0_block_end(lines: list[str], start: int) -> int:
    """Index of the col-0 line closing the block that opens at `start`."""
    depth = 0
    seen_open = False
    for i in range(start, len(lines)):
        line = lines[i]
        depth += line.count("{") - line.count("}")
        if "{" in line:
            seen_open = True
        if seen_open and depth <= 0:
            return i
    raise ValueError(f"no closing col-0 block after line {start + 1}")


def top_level_regions(lines: list[str]) -> list[tuple[int, int]]:
    regions: list[tuple[int, int]] = []
    depth = 0
    for i, line in enumerate(lines):
        if GUARD_OPEN.match(line):
            if depth == 0:
                regions.append(guard_span(lines, i))
            depth += 1
        elif line.startswith("#endif"):
            depth -= 1
    return regions


def parse_interior(
    lines: list[str], start: int, end: int, tests: list[tuple[int, int]]
) -> tuple[list[dict], int | None]:
    """Parse lines[start:end) into units. Returns (units, else_line_index)."""
    units: list[dict] = []
    else_line: int | None = None
    i = start
    while i < end:
        if else_line is None and (lines[i].startswith("#else") or lines[i].startswith("#elif")):
            else_line = i
            i += 1
            continue
        if any(a == i for a, _ in tests):
            a, b = next((a, b) for a, b in tests if a == i)
            units.append({"kind": "test", "lines": lines[a : b + 1], "pos": a})
            i = b + 1
            continue
        if GUARD_OPEN.match(lines[i]):
            o, c = guard_span(lines, i)
            units.append({"kind": "atomic", "lines": lines[o : c + 1],
                          "pos": o, "helper": True})
            i = c + 1
            continue
        if lines[i].strip() == "" or lines[i].startswith("//"):
            units.append({"kind": "filler", "lines": [lines[i]], "pos": i})
            i += 1
            continue
        if lines[i].rstrip().endswith("{"):
            be = col0_block_end(lines, i)
            units.append({"kind": "atomic", "lines": lines[i : be + 1],
                          "pos": i, "helper": True})
            i = be + 1
            continue
        units.append({"kind": "atomic", "lines": [lines[i]], "pos": i,
                      "helper": True})
        i += 1
    # fold fillers into previous units
    merged: list[dict] = []
    for u in units:
        if u["kind"] == "filler" and merged:
            merged[-1]["lines"].extend(u["lines"])
        else:
            merged.append(u)
    return merged, else_line


def parse_stream(lines: list[str]) -> tuple[list[str], list[dict]]:
    tests = find_test_blocks(lines)
    if not tests:
        raise ValueError("no TEST_CASE blocks found")
    regions = top_level_regions(lines)

    region_units: dict[int, dict] = {}
    for o, c in regions:
        inner = [(a, b) for a, b in tests if o <= a and b <= c]
        if not inner:
            continue
        units, else_line = parse_interior(lines, o + 1, c, tests)
        if else_line is not None:
            b1, b2 = [], []
            for u in units:
                (b1 if u["pos"] < else_line else b2).append(u)
            branches = [(None, b1), (lines[else_line], b2)]
        else:
            branches = [(None, units)]
        first_pos = units[0]["pos"] if units else c
        region_units[o] = {
            "kind": "region", "pos": o,
            "pre": lines[o:first_pos],
            "branches": branches,
            "endif": lines[c],
        }

    first_test = tests[0][0]
    # Prelude ends before the first test OR before a region that contains it.
    prelude_end = first_test
    for o, c in regions:
        if o < prelude_end and any(o <= a for a, _ in tests if a <= c):
            prelude_end = o
    prelude_repeat = lines[:prelude_end]

    stream: list[dict] = []
    i = prelude_end
    n = len(lines)
    while i < n:
        if i in region_units:
            stream.append(region_units[i])
            i = next(c for o, c in regions if o == i) + 1
            continue
        if any(a == i for a, _ in tests):
            a, b = next((a, b) for a, b in tests if a == i)
            stream.append({"kind": "test", "lines": lines[a : b + 1], "pos": a})
            i = b + 1
            continue
        if GUARD_OPEN.match(lines[i]):
            o, c = guard_span(lines, i)
            stream.append({"kind": "atomic", "lines": lines[o : c + 1],
                           "pos": o, "helper": True})
            i = c + 1
            continue
        if lines[i].strip() == "" or lines[i].startswith("//"):
            if stream:
                stream[-1].setdefault("_tail", []).append(lines[i])
            i += 1
            continue
        if lines[i].rstrip().endswith("{"):
            be = col0_block_end(lines, i)
            stream.append({"kind": "atomic", "lines": lines[i : be + 1],
                           "pos": i, "helper": True})
            i = be + 1
            continue
        stream.append({"kind": "atomic", "lines": [lines[i]], "pos": i,
                       "helper": True})
        i += 1

    for u in stream:
        tail = u.pop("_tail", None)
        if tail and "lines" in u:
            u["lines"] = list(u["lines"]) + tail
        elif tail and u["kind"] == "region":
            # attach to the last branch's last unit
            last_branch = u["branches"][-1][1]
            if last_branch:
                last_branch[-1]["lines"].extend(tail)
    return prelude_repeat, stream


DEF_NAME_PATTERNS = [
    re.compile(r"\b(?:struct|class|union|enum)\s+([A-Za-z_][A-Za-z0-9_]*)"),
    re.compile(r"^#define\s+([A-Za-z_][A-Za-z0-9_]*)"),
    re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\([^;{}]*$"),
    re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*(?:\[[^]]*\])?\s*(?:=[^=]|;)"),
]


def helper_names(u: dict) -> list[str]:
    """Defined symbol names of an atomic helper unit.

    For namespace blocks the whole body is scanned (the namespace itself may
    be anonymous and carries no name); for other units only the signature
    zone up to the first opening brace is scanned.
    """
    first = u["lines"][0].lstrip() if u["lines"] else ""
    if first.startswith("namespace"):
        text = "\n".join(u["lines"])
    else:
        sig: list[str] = []
        for line in u["lines"]:
            sig.append(line)
            if line.rstrip().endswith("{"):
                break
        text = "\n".join(sig)
    names: list[str] = []
    for rx in DEF_NAME_PATTERNS:
        for m in rx.finditer(text):
            name = m.group(1)
            if name and name not in {"if", "for", "while", "switch", "return"}:
                names.append(name)
    return sorted(set(names))


def word_present(text: str, name: str) -> bool:
    return re.search(r"\b" + re.escape(name) + r"\b", text) is not None


def region_unit_cost(u: dict, b1: int, b2: int) -> int:
    """Cost of a re-wrapped chunk: pre + b1 + [else] + b2 + endif."""
    branchy = len(u["branches"]) == 2
    return len(u["pre"]) + b1 + (1 if branchy else 0) + b2 + 1


def chunk_region(u: dict, budget: int) -> list[dict]:
    """Allocate region units greedily across re-wrapped chunks."""
    fixed = len(u["pre"]) + 1 + (1 if len(u["branches"]) == 2 else 0)
    if fixed > budget:
        raise ValueError(
            f"region re-wrap overhead ({fixed} lines) exceeds part budget")
    seq: list[tuple[int, dict]] = [
        (0, sub) for sub in u["branches"][0][1]
    ] + [
        (1, sub) for sub in u["branches"][1][1]
    ] if len(u["branches"]) == 2 else [
        (0, sub) for sub in u["branches"][0][1]
    ]
    chunks: list[dict] = []
    cur: list[tuple[int, dict]] = []
    sizes = [0, 0]
    for br, sub in seq:
        sz = len(sub["lines"])
        if sub["kind"] == "atomic" and sz > budget - fixed:
            raise ValueError(f"atomic unit inside region too large ({sz} lines)")
        if cur and region_unit_cost(u, sizes[0] + (sz if br == 0 else 0),
                                    sizes[1] + (sz if br == 1 else 0)) > budget:
            chunks.append({"kind": "region_chunk", "region": u,
                           "slice": cur})
            cur, sizes = [], [0, 0]
        cur.append((br, sub))
        sizes[br] += sz
    if cur:
        chunks.append({"kind": "region_chunk", "region": u, "slice": cur})
    return chunks


CARRY_RESERVE = 120  # per-part line reserve for reference-carried helpers


def pack_parts(prelude: list[str], stream: list[dict]) -> list[list[dict]]:
    base = len(prelude) + HEADER_LINES
    budget = MAX_LINES - base - CARRY_RESERVE
    if budget <= 0:
        raise ValueError("prelude exceeds the part budget")

    # Pre-chunk regions with the reserved budget.
    units: list[dict] = []
    for u in stream:
        if u["kind"] == "region":
            units.extend(chunk_region(u, budget))
        else:
            units.append(u)

    def unit_cost(u: dict) -> int:
        if u["kind"] == "region_chunk":
            sizes = [0, 0]
            for br, sub in u["slice"]:
                sizes[br] += len(sub["lines"])
            return region_unit_cost(u["region"], sizes[0], sizes[1])
        return len(u["lines"])

    # Greedy contiguous packing.
    parts: list[list[dict]] = []
    current: list[dict] = []
    size = base
    for u in units:
        sz = unit_cost(u)
        if current and size + sz > MAX_LINES - CARRY_RESERVE:
            parts.append(current)
            current, size = [], base
        current.append(u)
        size += sz
    if current:
        parts.append(current)

    # Rebalance loop: carry helpers by reference, then fix overflows by
    # moving trailing units of an overfull part into the next one.
    for _ in range(400):
        carried_sets = _reference_carry(parts, base)
        overflow = _first_overflow(parts, carried_sets, base)
        if overflow is None:
            return carried_sets, parts
        idx = overflow
        if len(parts[idx]) <= 1:
            raise ValueError(
                f"part {idx + 1} overflows with a single unsplittable unit")
        # Move trailing units until the part plausibly fits its carried set.
        moved_total = 0
        while len(parts[idx]) > 1:
            cand = parts[idx][-1]
            moved_total += unit_cost(cand)
            parts[idx + 1].insert(0, parts[idx].pop())
            if moved_total >= CARRY_RESERVE:
                break
    raise ValueError("rebalance did not converge")


def _part_own_text(part: list[dict]) -> str:
    chunks: list[str] = []
    for u in part:
        if u["kind"] == "region_chunk":
            for _, sub in u["slice"]:
                chunks.append("\n".join(sub["lines"]))
        else:
            chunks.append("\n".join(u["lines"]))
    return "\n".join(chunks)


def _reference_carry(parts: list[list[dict]], base: int) -> list[list[dict]]:
    """For each part, compute the list of helper units to prepend."""
    helpers: list[tuple[int, dict]] = []  # (part_index, unit)
    for i, part in enumerate(parts):
        for u in part:
            if u["kind"] == "atomic" and u.get("helper"):
                helpers.append((i, u))
    name_map: dict[int, list[str]] = {}
    everywhere: list[dict] = []
    for i, u in helpers:
        names = helper_names(u)
        if names:
            name_map[id(u)] = names
        else:
            everywhere.append(u)

    result: list[list[dict]] = []
    for i, part in enumerate(parts):
        own = _part_own_text(part)
        selected: list[dict] = list(everywhere)
        selected_ids = {id(u) for u in selected}
        changed = True
        while changed:
            changed = False
            for j, u in helpers:
                if j >= i or id(u) in selected_ids or id(u) not in name_map:
                    continue
                names = name_map[id(u)]
                if any(word_present(own, n) for n in names):
                    selected.append(u)
                    selected_ids.add(id(u))
                    own += "\n" + "\n".join(u["lines"])  # transitive refs
                    changed = True
        result.append(selected)
    return result


def _first_overflow(parts: list[list[dict]], carried_sets: list[list[dict]],
                    base: int) -> int | None:
    for i, part in enumerate(parts):
        total = base + sum(len(u["lines"]) for u in carried_sets[i])
        for u in part:
            if u["kind"] == "region_chunk":
                sizes = [0, 0]
                for br, sub in u["slice"]:
                    sizes[br] += len(sub["lines"])
                total += region_unit_cost(u["region"], sizes[0], sizes[1])
            else:
                total += len(u["lines"])
        if total > MAX_LINES:
            return i
    return None


def render_part(index: int, total: int, orig_name: str, prelude: list[str],
                carried: list[dict], units: list[dict]) -> str:
    """carried: helper units prepended verbatim before the part's own units."""
    out: list[str] = [
        f"// Part {index}/{total} of the original {orig_name},",
        "// split mechanically (TEST_CASE-preserving, guard-aware) by",
        "// tools/split_test_files.py to satisfy the 600-line test limit.",
        "",
    ]
    out.extend(prelude)
    for u in carried:
        out.extend(u["lines"])
    for u in units:
        if u["kind"] == "region_chunk":
            region = u["region"]
            branchy = len(region["branches"]) == 2
            out.extend(region["pre"])
            slices: dict[int, list[str]] = {0: [], 1: []}
            for br, sub in u["slice"]:
                slices[br].extend(sub["lines"])
            out.extend(slices[0])
            if branchy:
                out.append(region["branches"][1][0])
                out.extend(slices[1])
            out.append(region["endif"])
        else:
            out.extend(u["lines"])
    while out and out[-1] == "":
        out.pop()
    return "\n".join(out) + "\n"


def guard_balance(text: str) -> int:
    depth = 0
    for line in text.splitlines():
        if GUARD_OPEN.match(line):
            depth += 1
        elif line.startswith("#endif"):
            depth -= 1
    return depth


def count_tests(text: str) -> int:
    return sum(1 for line in text.splitlines() if TEST_CASE_OPEN.match(line))


def split_file(path: Path, dry: bool) -> list[tuple[Path, int]]:
    original = path.read_text(encoding="utf-8")
    prelude, stream = parse_stream(original.splitlines())
    carried_sets, parts = pack_parts(prelude, stream)
    if len(parts) == 1:
        print(f"  SKIP (fits): {path.name}")
        return []

    total = len(parts)
    rendered = [
        render_part(i + 1, total, path.name, prelude,
                    carried_sets[i], u)
        for i, u in enumerate(parts)
    ]

    if sum(count_tests(r) for r in rendered) != count_tests(original):
        raise ValueError("TEST_CASE census mismatch after split")
    for i, r in enumerate(rendered, 1):
        n = len(r.splitlines())
        if n > MAX_LINES:
            raise ValueError(f"part {i} has {n} lines > {MAX_LINES}")
        if guard_balance(r) != 0:
            raise ValueError(f"part {i} is guard-unbalanced")

    outs: list[tuple[Path, int]] = []
    for i, r in enumerate(rendered, 1):
        out = path.parent / f"{path.stem}_part{i}.cpp"
        if not dry:
            out.write_text(r, encoding="utf-8")
        outs.append((out, len(r.splitlines())))
    if not dry:
        path.unlink()
    return outs


def update_manifests(replacements: dict[str, list[str]], repo_root: Path) -> None:
    for site in MANIFEST_SITES:
        p = repo_root / site
        if not p.exists():
            continue
        lines = p.read_text(encoding="utf-8").splitlines()
        changed = False
        for orig_name, part_names in replacements.items():
            token_re = re.compile(
                r"(?<![A-Za-z0-9_.])" + re.escape(orig_name) +
                r"(?![A-Za-z0-9_.])")
            new_lines: list[str] = []
            for line in lines:
                m = token_re.search(line)
                if not m:
                    new_lines.append(line)
                    continue
                indent = line[: len(line) - len(line.lstrip())]
                head = line[: m.start()]
                tail = line[m.end():]
                dir_part = head[: head.rfind("/") + 1] if "/" in head else ""
                new_lines.append(f"{head}{part_names[0]}{tail}")
                new_lines.extend(f"{indent}{dir_part}{n}"
                                 for n in part_names[1:])
                changed = True
            lines = new_lines
        if changed:
            p.write_text("\n".join(lines) + "\n", encoding="utf-8")
            print(f"  updated {site}")


def main() -> int:
    dry = "--dry-run" in sys.argv
    repo_root = Path(__file__).resolve().parent.parent
    targets = [Path(a) for a in sys.argv[1:] if not a.startswith("--")]
    if not targets:
        found = []
        for p in sorted(repo_root.glob("tests/**/*.cpp")):
            n = len(p.read_text(encoding="utf-8").splitlines())
            if n > MAX_LINES:
                found.append((n, p))
        targets = [p for _, p in sorted(found, reverse=True)]
    if not targets:
        print("No oversized test files found.")
        return 0

    replacements: dict[str, list[str]] = {}
    failures: list[str] = []
    for t in targets:
        if not t.is_absolute():
            t = repo_root / t
        try:
            outs = split_file(t, dry)
            if outs:
                replacements[t.name] = [o.name for o, _ in outs]
                for o, n in outs:
                    print(f"  {'OK ' if n <= MAX_LINES else 'BIG'} "
                          f"{o.relative_to(repo_root)}: {n}")
        except Exception as exc:  # noqa: BLE001
            failures.append(f"{t.name}: {exc}")
            print(f"  FAIL {t.name}: {exc}")

    if failures:
        print(f"\n{len(failures)} failure(s); manifests NOT updated.")
        for f in failures:
            print(f"  - {f}")
        return 1
    if not dry and replacements:
        update_manifests(replacements, repo_root)
    print(f"\nSplit {len(replacements)} file(s) into "
          f"{sum(len(v) for v in replacements.values())} parts"
          f"{' (dry-run: nothing written)' if dry else ''}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
