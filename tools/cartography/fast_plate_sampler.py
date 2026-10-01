#!/usr/bin/env python3
"""FastPlateSampler - compose each pyramid level ONCE, crop per frame.

The engine's sample_continuous re-blits the same tiles for every frame; over
a 150-frame dive the identical plate is composited hundreds of times. This
sampler prepares one *union* plate per level (covering every anchor the shot
pans across, plus the worst-case frame extent: a level can serve as the
cross-fade's high layer, which asks for up to 2x the canvas), then a frame is
just an integer crop + the same resize/blend the engine performs. Pure
function of (anchor, zoom): deterministic, and byte-identical to the engine's
output up to the engine's own integer quantisation of the plate origin.

Falls back to pyramid.sample_continuous whenever a window would leave a
prepared plate, so a caller can never render garbage - only slow down.
"""

from __future__ import annotations

import math

import cv2
import numpy as np


class FastPlateSampler:
    def __init__(self, pyramid, width: int, height: int):
        self.pyr = pyramid
        self.W = int(width)
        self.H = int(height)
        # level -> list of (origin_x, origin_y, plate). One entry when the
        # union plate fits the budget; several per-anchor entries when the
        # anchors are too far apart for one plate to make sense.
        self.plates: dict[int, list[tuple[float, float, np.ndarray]]] = {}
        self.z_min = self.z_max = 0
        # Per-process scratch buffers. In the fork pool each worker faults
        # its own private copies on first write; every frame after that
        # reuses them instead of letting OpenCV malloc + page-fault fresh
        # 14 MB outputs per resize/blend. Same kernels, same bytes - only
        # the allocator churn is gone. Three slots: resize lo, resize hi,
        # blend (the blend reads slots 0 and 1 and writes slot 2).
        self._scratch: dict[tuple[int, int], list[np.ndarray]] = {}
        # Honest telemetry: frames a worker had to serve from the slow
        # engine because a window left a prepared plate. The harness
        # aggregates the per-block deltas into a run total; 0 means every
        # frame took the fast path.
        self.fallback_count = 0

    # -- the one-time cost ---------------------------------------------------
    MAX_PLATE_W = 9000   # ~145 MB per level; beyond this the union plate of
    MAX_PLATE_H = 5400   # far-apart anchors costs more RAM than it saves

    def prepare(self, anchors, z_min: int, z_max: int) -> None:
        """Compose the union plate of every level, covering all anchors.

        A level whose union extent would exceed the plate budget (anchors
        kilometres apart at fine zooms) is simply not prepared: sample()
        falls back to the engine for it, which is slow-but-correct instead of
        fast-but-swapped.
        """
        from dynamic_tile_pyramid import latlon_to_global_px

        self.z_min, self.z_max = int(z_min), int(z_max)
        # Worst case per level: a caller may sample the OVERSIZE warp canvas
        # (1.5x the delivery canvas) and this level may serve as the cross-
        # fade's high layer, which doubles the span again: 1.5 x 2 = 3x the
        # canvas (+ the engine's +4). A 2x extent here silently breaks the
        # warp-phase crops and falls back to the slow engine every frame.
        ext_w = 3 * self.W + 8
        ext_h = 3 * self.H + 8
        for z in range(self.z_min, self.z_max + 1):
            pts = [latlon_to_global_px(lat, lon, z) for lat, lon in anchors]
            xs = [x for x, _ in pts]
            ys = [y for _, y in pts]
            w = int(math.ceil(max(xs) - min(xs))) + ext_w
            h = int(math.ceil(max(ys) - min(ys))) + ext_h
            entries = []
            if w <= self.MAX_PLATE_W and h <= self.MAX_PLATE_H:
                cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
                left, top = cx - w / 2, cy - h / 2
                entries.append((left, top, self._compose_px(left, top, w, h, z)))
            else:
                # Far-apart anchors: one plate per anchor, so a multi-place
                # tour still runs the fast path (each stop crops its own
                # plate; only a window straddling two anchors falls back).
                for ax, ay in pts:
                    left, top = ax - ext_w / 2, ay - ext_h / 2
                    entries.append((left, top,
                                    self._compose_px(left, top, ext_w, ext_h, z)))
            if entries:
                self.plates[z] = entries

    def _compose_px(self, left: float, top: float, width: int, height: int,
                    zoom: int) -> np.ndarray:
        """Tile slicing identical to render_plate_at_zoom, px-addressed."""
        plate = np.zeros((height, width, 3), dtype=np.uint8)
        max_idx = 2 ** zoom
        for ty in range(int(math.floor(top / 256.0)),
                        int(math.ceil((top + height) / 256.0))):
            if ty < 0 or ty >= max_idx:
                continue
            y0, y1 = max(top, ty * 256.0), min(top + height, (ty + 1) * 256.0)
            if y1 <= y0:
                continue
            dy0, dy1 = int(round(y0 - top)), int(round(y1 - top))
            sy0, sy1 = int(round(y0 - ty * 256.0)), int(round(y1 - ty * 256.0))
            for tx in range(int(math.floor(left / 256.0)),
                            int(math.ceil((left + width) / 256.0))):
                x0, x1 = max(left, tx * 256.0), min(left + width, (tx + 1) * 256.0)
                if x1 <= x0:
                    continue
                dx0, dx1 = int(round(x0 - left)), int(round(x1 - left))
                sx0, sx1 = int(round(x0 - tx * 256.0)), int(round(x1 - tx * 256.0))
                key = (zoom, tx % max_idx, ty)
                tile = self.pyr.tile_cache.get(key)
                if tile is None:
                    _, tile = self.pyr._fetch_single_tile(zoom, tx, ty)
                    self.pyr.tile_cache[key] = tile
                hs = min(dy1 - dy0, sy1 - sy0)
                ws = min(dx1 - dx0, sx1 - sx0)
                if hs > 0 and ws > 0:
                    plate[dy0:dy0 + hs, dx0:dx0 + ws] = \
                        tile[sy0:sy0 + hs, sx0:sx0 + ws]
        return plate

    # -- the per-frame cost ---------------------------------------------------
    def __getattr__(self, name):
        # Everything the engine exposes (tile_cache, render_plate_at_zoom,
        # prefetch_pyramid, ...) delegates to the wrapped pyramid, so callers
        # - including the acceptance suites - duck-type this as the engine.
        return getattr(self.pyr, name)

    def sample_continuous(self, lat: float, lon: float, zoom: float,
                          width: int | None = None,
                          height: int | None = None) -> np.ndarray:
        """Engine-compatible alias for sample()."""
        return self.sample(lat, lon, zoom, width, height)

    def _crop(self, z: int, anchor_px: tuple[float, float],
              w: int, h: int) -> np.ndarray | None:
        for ox, oy, plate in self.plates.get(z, ()):
            x0 = int(round(anchor_px[0] - ox - w / 2))
            y0 = int(round(anchor_px[1] - oy - h / 2))
            if x0 >= 0 and y0 >= 0 and x0 + w <= plate.shape[1] \
                    and y0 + h <= plate.shape[0]:
                return plate[y0:y0 + h, x0:x0 + w]
        return None

    def _scratch_buf(self, w: int, h: int, slot: int) -> np.ndarray:
        bufs = self._scratch.get((w, h))
        if bufs is None:
            bufs = [np.empty((h, w, 3), dtype=np.uint8) for _ in range(3)]
            self._scratch[(w, h)] = bufs
        return bufs[slot]

    def sample(self, lat: float, lon: float, zoom: float,
               width: int | None = None, height: int | None = None) -> np.ndarray:
        """The engine's sample_continuous, fed from prepared plates."""
        from dynamic_tile_pyramid import latlon_to_global_px

        W = self.W if width is None else int(width)
        H = self.H if height is None else int(height)
        z_lo = int(math.floor(zoom))
        t = zoom - z_lo
        alpha = t * t * (3.0 - 2.0 * t)
        scale_lo = 2.0 ** t

        if not (self.z_min <= z_lo <= self.z_max):
            self.fallback_count += 1
            return self.pyr.sample_continuous(lat, lon, zoom, W, H)

        w_lo = int(round(W / scale_lo)) + 4
        h_lo = int(round(H / scale_lo)) + 4
        lo_px = latlon_to_global_px(lat, lon, z_lo)
        lo_crop = self._crop(z_lo, lo_px, w_lo, h_lo)
        if lo_crop is None:
            self.fallback_count += 1
            return self.pyr.sample_continuous(lat, lon, zoom, W, H)
        plate_lo = cv2.resize(lo_crop, (W, H), interpolation=cv2.INTER_LINEAR,
                              dst=self._scratch_buf(W, H, 0))

        if alpha <= 0.001 or z_lo + 1 > self.z_max:
            return plate_lo

        scale_hi = 2.0 ** (t - 1.0)
        w_hi = int(round(W / scale_hi)) + 4
        h_hi = int(round(H / scale_hi)) + 4
        hi_px = latlon_to_global_px(lat, lon, z_lo + 1)
        hi_crop = self._crop(z_lo + 1, hi_px, w_hi, h_hi)
        if hi_crop is None:
            self.fallback_count += 1
            return self.pyr.sample_continuous(lat, lon, zoom, W, H)
        plate_hi = cv2.resize(hi_crop, (W, H), interpolation=cv2.INTER_LINEAR,
                              dst=self._scratch_buf(W, H, 1))
        return cv2.addWeighted(plate_lo, 1.0 - alpha, plate_hi, alpha, 0.0,
                               dst=self._scratch_buf(W, H, 2))
