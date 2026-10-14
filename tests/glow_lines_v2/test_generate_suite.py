#!/usr/bin/env python3
"""Unit-level acceptance tests for the documentary Glow Lines 2.0 plan generator."""
from __future__ import annotations
import importlib.util
import pathlib
import unittest

HERE = pathlib.Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("glow_lines_v2_generator", HERE / "generate_suite.py")
generator = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(generator)


class GlowLinesV2ContractTests(unittest.TestCase):
    def plans(self):
        return [(case, generator.make_scene(case, pathlib.Path(f"/tmp/{case['slug']}.mp4")))
                for case in generator.CASES]

    def test_exact_six_durations_and_video_contract(self):
        self.assertEqual([case["duration"] for case in generator.CASES], [5, 5, 6, 5, 7, 8])
        self.assertTrue(all(not case.get("temporal_motion_blur") for case in generator.CASES))
        self.assertEqual(sum(case["duration"] for case in generator.CASES), 36)
        self.assertEqual(sum(case["duration"] for case in generator.CASES) * 30, 1080)
        self.assertEqual(len(generator.CASES), 6)
        for case, plan in self.plans():
            with self.subTest(case=case["slug"]):
                generator.verify_contract(plan, case)
                self.assertEqual(plan["canvas"], {
                    "width": 1920, "height": 1080, "fps_num": 30, "fps_den": 1,
                    "duration_frames": case["duration"] * 30,
                })
                self.assertEqual(plan["output"]["codec"], "h264")
                self.assertEqual(plan["output"]["color_space"], "rec709")
                self.assertNotIn("temporal_motion_blur", plan)

    def test_reveal_is_equal_arc_distance_on_one_shared_linear_clock(self):
        for case, plan in self.plans():
            if not case.get("draw"):
                continue
            with self.subTest(case=case["slug"]):
                paths = [layer for layer in plan["layers"]
                         if layer.get("shape", {}).get("type") == "path"]
                core, emission = paths[-1], next(layer for layer in paths if layer.get("effects"))
                core_track = next(t for t in core["animation"]["tracks"]
                                  if t["property"] == "stroke_dash_offset")
                glow_track = next(t for t in emission["animation"]["tracks"]
                                  if t["property"] == "stroke_dash_offset")
                self.assertEqual(core_track, glow_track)
                frames = [key["frame"] for key in core_track["keyframes"]]
                self.assertEqual(frames, sorted(set(frames)))
                self.assertEqual(frames[0], 24)
                self.assertLess(frames[-1], case["duration"] * 30)
                values = [key["value"] for key in core_track["keyframes"]]
                distances = [values[i] - values[i + 1] for i in range(len(values) - 1)]
                self.assertTrue(all(distance > 0 for distance in distances))
                self.assertLess(max(distances) - min(distances), max(distances) * 0.04)
                self.assertEqual(core_track["easing"], "linear")

    def test_glow_is_real_separate_gaussian_underlay_and_not_a_head_blob(self):
        for case, plan in self.plans():
            if not case.get("glow"):
                continue
            with self.subTest(case=case["slug"]):
                paths = [layer for layer in plan["layers"]
                         if layer.get("shape", {}).get("type") == "path"]
                self.assertEqual(len(paths), 2)
                self.assertEqual(paths[0]["shape"]["path"], paths[1]["shape"]["path"])
                self.assertEqual(paths[0]["size"], paths[1]["size"])
                self.assertEqual(paths[0]["position"], paths[1]["position"])
                self.assertEqual(paths[0]["effects"][0]["type"], "glow")
                self.assertGreater(paths[0]["effects"][0]["intensity"], 0)
                self.assertEqual(paths[0]["effects"][0]["color"], generator.rgba(case["glow"]["hex_color"]))
                self.assertLessEqual(paths[0]["effects"][0]["radius"], 6)
                self.assertLessEqual(paths[0]["effects"][0]["intensity"], 0.30)
                self.assertFalse(any(layer.get("shape", {}).get("type") == "ellipse"
                                     for layer in plan["layers"]))

    def test_decay_keeps_the_base_stroke_and_reduces_only_emission(self):
        case = next(case for case in generator.CASES if case.get("decay"))
        plan = generator.make_scene(case, pathlib.Path("/tmp/decay.mp4"))
        generator.verify_contract(plan, case)
        paths = [layer for layer in plan["layers"]
                 if layer.get("shape", {}).get("type") == "path"]
        core = next(layer for layer in paths if not layer.get("effects"))
        emission = next(layer for layer in paths if layer.get("effects"))
        core_opacity = next(track for track in core["animation"]["tracks"]
                            if track["property"] == "opacity")
        emission_opacity = next(track for track in emission["animation"]["tracks"]
                                if track["property"] == "opacity")
        glow_intensity = next(track for track in emission["effect_param_tracks"]
                              if track["effect_id"] == "light.glow" and track["param"] == "intensity")
        self.assertEqual(core_opacity["keyframes"][-1]["value"], 1.0)
        self.assertEqual(emission_opacity["keyframes"][-1]["value"], 0.35)
        values = [key["value"] for key in glow_intensity["keyframes"]]
        frames = [key["frame"] for key in glow_intensity["keyframes"]]
        self.assertEqual(frames, [0, 24, 70, 87, 149])
        self.assertGreater(values[1], values[-1])
        self.assertAlmostEqual(values[-1], 0.0)
        self.assertEqual(values[1], values[2])
        self.assertEqual(values[3], values[4])
        self.assertGreater(values[1], values[-2])
        self.assertEqual(core["shape"]["path"], emission["shape"]["path"])

    def test_editorial_layout_uses_content_plates_and_no_neon_particles(self):
        forbidden = {"#42D9FF", "#FF6B8A", "#A98BFF", "#FFD166", "#52E0A0"}
        for case, plan in self.plans():
            with self.subTest(case=case["slug"]):
                self.assertFalse(any(layer.get("type") == "particles" for layer in plan["layers"]))
                for layer in plan["layers"]:
                    stroke = layer.get("shape", {}).get("stroke", {})
                    self.assertNotIn(stroke.get("color"), forbidden)
                    self.assertNotEqual(layer.get("id"), "photo_shade")
                if case.get("background_asset"):
                    self.assertTrue(any(layer.get("id") == "title_rail" for layer in plan["layers"]))
                if case.get("evidence"):
                    self.assertEqual(sum(layer.get("type") == "image" for layer in plan["layers"]), 1)
                    self.assertTrue({"archive_plate", "field_note_panel", "field_body", "photo_caption", "connection_note"}.issubset(
                        {layer["id"] for layer in plan["layers"]}))
                    self.assertEqual(sum(layer.get("type") == "image" for layer in plan["layers"]), 1)
                    route = next(layer for layer in plan["layers"] if layer.get("id") == "route_0")
                    self.assertLess(route["position"][0] - route["size"][0] / 2, 1400)
                    self.assertGreater(route["position"][0] + route["size"][0] / 2, 1400)

    def test_decay_roi_metric_is_a_strict_measured_render_gate(self):
        metric = generator.decay_halo_reduction([2.0, 10.0, 8.0], minimum_reduction=0.12)
        self.assertAlmostEqual(metric["relative_reduction"], 0.25)
        with self.assertRaises(ValueError):
            generator.decay_halo_reduction([2.0, 10.0, 9.1], minimum_reduction=0.12)
        with self.assertRaises(ValueError):
            generator.decay_halo_reduction([0.0, 0.0, 0.0], minimum_reduction=0.12)
        with self.assertRaises(ValueError):
            generator.decay_halo_reduction([4.0, 3.0, 2.0], minimum_reduction=0.12)
        self.assertAlmostEqual(generator.decay_intensity_reduction([1.0, 9.0, 1.0])["relative_reduction"], 1.0)
        self.assertEqual(generator.decay_sample_frames({"layers": [{"id": "route_glow_0", "effects": ["glow"],
            "effect_param_tracks": [{"effect_id": "light.glow", "param": "intensity", "keyframes": [
                {"frame": 0}, {"frame": 24}, {"frame": 70}, {"frame": 87}]}]}]}, 150), (70, 95))

    def test_halo_roi_excludes_the_core_and_metric_uses_a_fixed_route_mask(self):
        case = next(case for case in generator.CASES if case.get("decay"))
        plan = generator.make_scene(case, pathlib.Path("/tmp/decay.mp4"))
        roi = generator.route_halo_roi(plan, case)
        self.assertEqual(roi.shape, (1080, 1920))
        self.assertGreater(int(roi.sum()), 0)
        self.assertGreaterEqual(generator.halo_signal(generator.Image.new("RGB", (1920, 1080), (255, 0, 0)), roi), 0.0)
        self.assertLessEqual(generator.halo_signal(generator.Image.new("RGB", (1920, 1080), (20, 20, 20)), roi),
                             generator.halo_signal(generator.Image.new("RGB", (1920, 1080), (255, 0, 0)), roi))
        self.assertGreater(generator.core_signal(generator.Image.new("RGB", (1920, 1080), (255, 255, 255)), plan, case), 250.0)

    def test_measured_decay_metric_rejects_non_finite_values(self):
        import math
        for samples in ([math.nan, 0.0, 0.0], [math.inf, 1.0, 1.0], [-1.0, -2.0, -3.0]):
            with self.subTest(samples=samples), self.assertRaises(ValueError):
                generator.decay_halo_reduction(samples)

    def test_failure_report_is_not_a_stale_pass(self):
        import json
        import subprocess
        import tempfile
        with tempfile.TemporaryDirectory() as directory:
            script = HERE / "generate_suite.py"
            result = subprocess.run(["python3", str(script), "--cli", str(HERE / "missing-cli"),
                                     "--output-root", directory], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            report = json.loads((pathlib.Path(directory) / "report.json").read_text())
            self.assertEqual(report["status"], "FAIL")
            self.assertIn("missing-cli", report["acceptance"]["blocking_gates"][0])

    def test_all_strokes_fit_tight_effect_bounded_surfaces(self):
        for case, plan in self.plans():
            for layer in plan["layers"]:
                shape = layer.get("shape", {})
                if shape.get("type") != "path":
                    continue
                with self.subTest(case=case["slug"], layer=layer["id"]):
                    self.assertLess(layer["size"][0], 1400 if case.get("evidence") else 900)
                    self.assertLess(layer["size"][1], 500)


if __name__ == "__main__":
    unittest.main(verbosity=2)
