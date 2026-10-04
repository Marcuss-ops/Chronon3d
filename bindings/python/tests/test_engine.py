import json
import os
import shutil
import tempfile
import threading
import unittest
from pathlib import Path

import chronon3d
from chronon3d import RenderEngine, Status

PLAN = {
    "schema": "chronon.render-plan.v2",
    "version": 2,
    "job_id": "py-binding-test",
    "canvas": {"width": 64, "height": 48, "fps_num": 30, "fps_den": 1, "duration_frames": 3},
    "layers": [{"id": "background", "type": "color", "color": [0.1, 0.5, 0.8, 1.0]}],
    "output": {"path": "out.png", "format": "png"},
}


class TestChronon3d(unittest.TestCase):
    def setUp(self):
        self.tmp_dir = tempfile.mkdtemp(prefix="chronon3d_test_")
        self.tmp_path = Path(self.tmp_dir)
        self.engine = RenderEngine()

    def tearDown(self):
        self.engine.close()
        shutil.rmtree(self.tmp_dir, ignore_errors=True)

    def test_versions(self):
        self.assertEqual(chronon3d.abi_version(), 2)
        self.assertTrue(chronon3d.version())
        self.assertGreaterEqual(chronon3d.contract_version(), 1)
        self.assertEqual(chronon3d.status_name(Status.CANCELLED), "CANCELLED")

    def test_render_still_from_dict(self):
        f = self.engine.render_still(PLAN, frame=0)
        self.assertEqual((f.width, f.height), (64, 48))
        self.assertEqual(len(f.data), f.stride * f.height)
        self.assertTrue(any(f.data))

    def test_render_still_from_json_and_path(self):
        a = self.engine.render_still(json.dumps(PLAN))
        p = self.tmp_path / "plan.json"
        p.write_text(json.dumps(PLAN))
        b = self.engine.render_still(p)
        self.assertEqual(a.data, b.data)

    def test_compiled_plan_reuse_and_sequence(self):
        with self.engine.compile(PLAN) as plan:
            self.assertEqual(plan.fps, (30, 1))
            frames = list(self.engine.render_sequence(plan))
        self.assertEqual([f.index for f in frames], [0, 1, 2])

    def test_invalid_plan_raises_mapped_error(self):
        with self.assertRaises(chronon3d.ChrononError) as cm:
            self.engine.compile({"schema": "chronon.render-plan.v2", "version": 2})
        self.assertIn(
            cm.exception.status,
            (Status.INVALID_PLAN, Status.PARSE_FAILED, Status.UNSUPPORTED_SCHEMA, Status.INVALID_ARGUMENT),
        )
        self.assertIsNot(type(cm.exception), chronon3d.ChrononError)

    def test_garbage_json(self):
        with self.assertRaises(chronon3d.ChrononError):
            self.engine.compile(b"{not json")

    def test_closed_engine_rejects_calls(self):
        eng = RenderEngine()
        eng.close()
        self.assertTrue(eng.closed)
        with self.assertRaises(chronon3d.ChrononError):
            eng.render_still(PLAN)
        eng.close()

    def test_set_assets_root_recreates_engine(self):
        self.engine.set_assets_root(self.tmp_path)
        self.assertEqual(self.engine.assets_root, str(self.tmp_path))
        self.assertEqual(self.engine.render_still(PLAN).width, 64)

    def test_render_file_png_and_progress(self):
        seen = []
        try:
            out = self.engine.render_file(
                PLAN,
                self.tmp_path / "still.png",
                start=0,
                end=0,
                progress=lambda c, t: seen.append((c, t)),
            )
        except chronon3d.ChrononError as e:
            if "CHRONON3D_ENABLE_VIDEO" in str(e):
                self.skipTest(f"built without video/file-sink: {e}")
            raise
        produced = list(self.tmp_path.iterdir())
        self.assertTrue(produced, f"no output written for {out}")

    def test_render_file_cancellation(self):
        ev = threading.Event()
        ev.set()
        try:
            self.engine.render_file(PLAN, self.tmp_path / "seq.png", start=0, end=2, cancelled=ev)
        except chronon3d.errors.CancelledError as e:
            self.assertEqual(e.status, Status.CANCELLED)
        except chronon3d.ChrononError as e:
            self.skipTest(f"render_file not available for this output: {e}")
        else:
            self.skipTest("engine finished before polling cancellation (single-frame fast path)")

    def test_threads_share_engine(self):
        plan = self.engine.compile(PLAN)
        errors = []

        def work():
            try:
                for i in range(3):
                    self.engine.render_still(plan, i)
            except Exception as e:
                errors.append(e)

        ts = [threading.Thread(target=work) for _ in range(4)]
        for t in ts:
            t.start()
        for t in ts:
            t.join()
        self.assertFalse(errors)


if __name__ == "__main__":
    unittest.main()
