#!/usr/bin/env python3
"""Ensure every catalogued effect has a software processor registration."""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / "include/chronon3d/effects/effect_catalog.def"
REGISTRY = ROOT / "src/backends/software/processors/builtin_processors.cpp"


def main() -> int:
    catalog_text = CATALOG.read_text(encoding="utf-8")
    registry_text = REGISTRY.read_text(encoding="utf-8")

    catalog_params = re.findall(
        r"CHRONON_EFFECT\(\s*\d+\s*,\s*\w+\s*,\s*(\w+)", catalog_text
    )
    registered_params = set(
        re.findall(r"register_effect_processor<\s*(\w+)\s*>", registry_text)
    )

    duplicates = sorted(
        name for name in set(catalog_params) if catalog_params.count(name) > 1
    )
    missing = sorted(set(catalog_params) - registered_params)
    extra = sorted(registered_params - set(catalog_params))

    if duplicates or missing or extra:
        print("GATE_FAIL: software effect processor coverage is inconsistent")
        if duplicates:
            print("duplicate catalog parameters:", ", ".join(duplicates))
        if missing:
            print("missing software processors:", ", ".join(missing))
        if extra:
            print("unlisted software processors:", ", ".join(extra))
        return 1

    print(
        f"GATE_PASS: {len(catalog_params)} catalogued effects have exactly one software processor"
    )
    print(
        "[INFO] check_effect_processor_coverage: catalog and software registry are aligned"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
