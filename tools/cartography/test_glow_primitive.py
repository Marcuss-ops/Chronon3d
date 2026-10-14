#!/usr/bin/env python3
"""Numerical conformance tests for the shared engine-law map halo."""
from __future__ import annotations

import sys
import unittest
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from glow_primitive import composite_halo, gaussian_halo


class GlowPrimitiveTests(unittest.TestCase):
    def test_empty_mask_and_zero_intensity_are_transparent(self):
        mask = np.zeros((25, 31), np.uint8)
        self.assertFalse(np.any(gaussian_halo(mask, (20, 100, 255), 4.0)))
        mask[12, 15] = 255
        self.assertFalse(np.any(gaussian_halo(mask, (20, 100, 255), 4.0, 0.0)))

    def test_halo_is_finite_bounded_and_has_gaussian_spread(self):
        mask = np.zeros((81, 81), np.uint8)
        mask[34:47, 34:47] = 255
        halo = gaussian_halo(mask, (40, 120, 240), 5.0)
        self.assertEqual(halo.shape, (81, 81, 3))
        self.assertEqual(halo.dtype, np.uint8)
        self.assertTrue(np.isfinite(halo).all())
        self.assertGreater(int(halo[40, 40].max()), 0)
        self.assertGreater(int(halo[40, 45].max()), 0)
        self.assertGreater(int(halo[40, 45].max()), int(halo[40, 65].max()))
        self.assertLessEqual(int(halo[40, 20].max()), int(halo[40, 60].max()) + 1)

    def test_additive_composite_changes_only_from_generated_halo(self):
        frame = np.full((31, 31, 3), 255, np.uint8)
        before = frame.copy()
        mask = np.zeros((31, 31), np.uint8)
        mask[12:19, 12:19] = 255
        result = composite_halo(frame, mask, (255, 255, 255), 3.0)
        self.assertIs(result, frame)
        self.assertTrue(np.array_equal(frame, before))  # saturated channels clamp
        frame.fill(0)
        composite_halo(frame, mask, (20, 100, 240), 3.0)
        self.assertGreater(int(frame[15, 15].max()), 0)
        self.assertEqual(int(frame[0, 0].max()), 0)

    def test_invalid_shapes_parameters_and_destination_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "2D uint8"):
            gaussian_halo(np.zeros((5, 5, 3), np.uint8), (0, 0, 0), 1.0)
        with self.assertRaisesRegex(ValueError, "sigma"):
            gaussian_halo(np.zeros((5, 5), np.uint8), (0, 0, 0), -1.0)
        with self.assertRaisesRegex(ValueError, "dimensions"):
            composite_halo(np.zeros((5, 6, 3), np.uint8), np.zeros((5, 5), np.uint8),
                           (0, 0, 0), 1.0)


if __name__ == "__main__":
    unittest.main()
