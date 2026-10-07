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
    def test_ranks_measured_fallback_usage_and_effect_time(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            sidecar = Path(directory) / "job.timing.json"
            sidecar.write_text(json.dumps({
                "job": {
                    "effect_usage": {
                        "effect.color.vignette": {
                            "native_frames": 2,
                            "admission_rejected_frames": 0,
                            "software_dispatches": 2,
                            "effect_time_ns": 1500000,
                            "fallback_count": 2,
                            "fallback_effect_time_ns": 1000000,
                        },
                        "effect.color.curves": {
                            "native_frames": 3,
                            "admission_rejected_frames": 1,
                            "software_dispatches": 4,
                            "effect_time_ns": 9000000,
                            "fallback_count": 1,
                            "fallback_effect_time_ns": 2000000,
                        },
                    },
                },
            }), encoding="utf-8")

            report = TIMING.aggregate_effect_usage([sidecar])

        self.assertEqual(report["source_sidecars"], 1)
        self.assertEqual([row["effect"] for row in report["effects"]], [
            "effect.color.vignette", "effect.color.curves",
        ])
        self.assertEqual(report["effects"][0]["freq"], 4.0)
        self.assertEqual(report["effects"][0]["native_frames"], 2.0)
        self.assertEqual(report["effects"][0]["admission_rejected_frames"], 0.0)
        self.assertEqual(report["effects"][0]["software_dispatches"], 2.0)
        self.assertEqual(report["effects"][0]["effect_time_ms"], 1.5)
        self.assertEqual(report["effects"][0]["fallback_count"], 2.0)
        self.assertEqual(report["effects"][0]["fallback_effect_time_ms"], 1.0)
        self.assertEqual(report["effects"][1]["effect_time_ms"], 9.0)
        self.assertEqual(report["effects"][1]["fallback_count"], 1.0)
        self.assertEqual(report["effects"][1]["fallback_effect_time_ms"], 2.0)

    def test_legacy_sidecars_produce_explicitly_empty_effect_rows(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            sidecar = Path(directory) / "legacy.timing.json"
            sidecar.write_text(json.dumps({"job": {"gpu": {}}}), encoding="utf-8")
            report = TIMING.aggregate_effect_usage([sidecar])

        self.assertEqual(report["source_sidecars"], 1)
        self.assertEqual(report["effects"], [])
        self.assertIn("predate", report["data_coverage"])
        self.assertEqual(report["schema"], "chronon3d.effect-usage.v2")


if __name__ == "__main__":
    unittest.main()
