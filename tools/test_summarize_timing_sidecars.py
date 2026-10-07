"""Focused tests for event-only effect usage aggregation."""
from __future__ import annotations

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).with_name("summarize_timing_sidecars.py")
SPEC = importlib.util.spec_from_file_location("timing_sidecars", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
TIMING = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TIMING)


class EffectUsageAggregationTests(unittest.TestCase):
    def test_ranks_counts_and_apportions_available_effect_stack_time(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            sidecar = Path(directory) / "job.timing.json"
            sidecar.write_text(json.dumps({
                "job": {
                    "effect_usage": {
                        "effect.color.vignette": {
                            "native_frames": 2,
                            "admission_rejected_frames": 0,
                        },
                        "effect.color.curves": {
                            "native_frames": 3,
                            "admission_rejected_frames": 1,
                        },
                    },
                    "cpu_breakdown": {"effect_stack_total_ms": 12.0},
                },
            }), encoding="utf-8")

            report = TIMING.aggregate_effect_usage([sidecar])

        self.assertEqual(report["source_sidecars"], 1)
        self.assertEqual([row["effect"] for row in report["effects"]], [
            "effect.color.curves", "effect.color.vignette",
        ])
        self.assertEqual(report["effects"][0]["freq"], 4.0)
        self.assertEqual(report["effects"][0]["native_frames"], 3.0)
        self.assertEqual(report["effects"][0]["admission_rejected_frames"], 1.0)
        self.assertEqual(report["effects"][0]["est_ms"], 8.0)
        self.assertEqual(report["effects"][1]["est_ms"], 4.0)

    def test_legacy_sidecars_produce_explicitly_empty_effect_rows(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            sidecar = Path(directory) / "legacy.timing.json"
            sidecar.write_text(json.dumps({"job": {"gpu": {}}}), encoding="utf-8")
            report = TIMING.aggregate_effect_usage([sidecar])

        self.assertEqual(report["source_sidecars"], 1)
        self.assertEqual(report["effects"], [])
        self.assertIn("predate", report["data_coverage"])


if __name__ == "__main__":
    unittest.main()
