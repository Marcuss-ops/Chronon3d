#!/usr/bin/env python3
"""Source-level contract tests for daemon render admission policy."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "apps/chronon3d_cli/daemon/daemon_render_concurrency.hpp"


class RenderAdmissionContract(unittest.TestCase):
    def test_fullgraph_is_measured_and_monolithic_always_serial(self):
        source = HEADER.read_text()
        self.assertIn("kMeasuredFullGraphPeakBytes", source)
        self.assertRegex(source, r"if\s*\(input\.job_class\s*==\s*RenderJobClass::MonolithicSamePlan\)\s*\{")
        self.assertIn("monolithic same-plan render: the whole timeline is one execution domain", source)
        self.assertIn("kSerializedRenderJobSlots = 1", source)

    def test_disjoint_overlap_requires_two_footprints_and_live_free_memory(self):
        source = HEADER.read_text()
        self.assertIn("decision.peak_footprints_that_fit < 2U", source)
        self.assertIn("input.device_free_vram_bytes < input.job_peak_vram_bytes", source)
        self.assertIn("RenderAdmission::ParallelDisjoint", source)

    def test_policy_does_not_promote_producer_slots_to_daemon_slots(self):
        source = HEADER.read_text()
        self.assertIn('"CHRONON_RENDER_JOB_SLOTS"', source)
        self.assertIn("kRenderJobExecutionSerialized ? kSerializedRenderJobSlots : requested", source)
        self.assertRegex(source, r"resources\.render_job_slots\s*=\s*1U")


if __name__ == "__main__":
    unittest.main()
