#!/usr/bin/env python3
"""Lock the versioned GPU baseline corpus shape without requiring a GPU host."""

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "docs/benchmarks/gpu-corpus-v1.json"


def test_manifest_is_complete_and_unique() -> None:
    data = json.loads(MANIFEST.read_text())
    assert data["id"] == "chronon3d-gpu-corpus-v1"
    cases = data["cases"]
    assert len(cases) == 17
    ids = [case["id"] for case in cases]
    assert len(ids) == len(set(ids))
    assert ids[:12] == [f"B{i:02d}" for i in range(12)]
    assert ids[12:] == [f"GPU_TEXT_{i:02d}" for i in range(1, 6)]
    assert all(case["scene"] and case["category"] for case in cases)


if __name__ == "__main__":
    test_manifest_is_complete_and_unique()
    print("gpu corpus manifest tests: PASS")
