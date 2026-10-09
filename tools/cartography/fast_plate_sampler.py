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
import time

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
        self.prepare_telemetry = {
            "prepare_ms": 0.0,
            "plate_compose_ms": 0.0,
            "plate_count": 0,
            "plate_bytes": 0,
            "required_tile_count": 0,
        }
        # Honest telemetry: frames a worker had to serve from the slow
        # engine because a window left a prepared plate. The harness
        # aggregates the per-block deltas into a run total; 0 means every
        # frame took the fast path.
        self.fallback_count = 0
        self.opencl_plates = None

    def prepare_opencl(self) -> int:
        """Upload prepared map plates once so per-frame crops stay on GPU."""
        if not cv2.ocl.useOpenCL():
            raise RuntimeError("OpenCL must be active before uploading map plates")
        uploaded = {}
        total_bytes = 0
        for zoom, entries in self.plates.items():
            gpu_entries = []
            for left, top, plate in entries:
                gpu_plate = cv2.UMat(np.ascontiguousarray(plate))
                gpu_entries.append((left, top, gpu_plate, plate.shape[1], plate.shape[0]))
                total_bytes += int(plate.nbytes)
            uploaded[zoom] = gpu_entries
        self.opencl_plates = uploaded
        return total_bytes

    def sample_opencl(self, lat: float, lon: float, zoom: float,
                      width: int | None = None,
                      height: int | None = None):
        """Sample prepared map plates with OpenCV's OpenCL GPU kernels.

        Geographic coordinate math and tile preparation stay on the host; the
        full-resolution resampling and fractional zoom blend run as UMat work
        on the selected OpenCL device.
        """
        from dynamic_tile_pyramid import latlon_to_global_px

        if not cv2.ocl.haveOpenCL():
            raise RuntimeError("OpenCL GPU map rendering is unavailable")
        cv2.ocl.setUseOpenCL(True)
        if not cv2.ocl.useOpenCL():
            raise RuntimeError("OpenCV did not activate the OpenCL map renderer")
        W = self.W if width is None else int(width)
        H = self.H if height is None else int(height)
        z_lo = int(math.floor(zoom))
        t = zoom - z_lo
        alpha = t * t * (3.0 - 2.0 * t)
        if not (self.z_min <= z_lo <= self.z_max):
            raise RuntimeError(f"OpenCL map sample zoom {zoom:.3f} leaves prepared plates")

        if self.opencl_plates is None:
            raise RuntimeError("OpenCL map plates were not uploaded")

        def resize_level(z: int, scale: float):
            w = int(round(W / scale)) + 4
            h = int(round(H / scale)) + 4
            anchor_px = latlon_to_global_px(lat, lon, z)
            crop = None
            for origin_x, origin_y, plate, plate_w, plate_h in self.opencl_plates.get(z, ()):
                x0 = int(round(anchor_px[0] - origin_x - w / 2))
                y0 = int(round(anchor_px[1] - origin_y - h / 2))
                if x0 >= 0 and y0 >= 0 and x0 + w <= plate_w \
                        and y0 + h <= plate_h:
                    crop = cv2.UMat(plate, (x0, y0, w, h))
                    break
            if crop is None:
                raise RuntimeError(f"OpenCL map sample has no prepared plate at zoom {z}")
            return cv2.resize(crop, (W, H), interpolation=cv2.INTER_LINEAR)

        lo = resize_level(z_lo, 2.0 ** t)
        if alpha <= 0.001 or z_lo + 1 > self.z_max:
            return lo
        hi = resize_level(z_lo + 1, 2.0 ** (t - 1.0))
        return cv2.addWeighted(lo, 1.0 - alpha, hi, alpha, 0.0)

    # -- the one-time cost ---------------------------------------------------
    MAX_PLATE_W = 9000   # ~145 MB per level; beyond this the union plate of
    MAX_PLATE_H = 5400   # far-apart anchors costs more RAM than it saves

    def prepare(self, anchors, z_min: int, z_max: int) -> None:
        """Prefetch exact plate coverage, then compose each plate once.

        A level whose union extent would exceed the plate budget (anchors
        kilometres apart at fine zooms) gets one plate per anchor. The exact
        tile set is computed before composition and fetched concurrently, so
        `_compose_px` never performs network I/O in its nested pixel/tile loop.
        """
        from dynamic_tile_pyramid import latlon_to_global_px

        if not anchors:
            raise ValueError("FastPlateSampler.prepare requires at least one geographic anchor")
        if z_min < 0 or z_max < z_min:
            raise ValueError(f"invalid prepared zoom range {z_min}..{z_max}")
        prepare_started = time.perf_counter()
        self.plates.clear()
        self.prepare_telemetry = {
            "prepare_ms": 0.0,
            "plate_compose_ms": 0.0,
            "plate_count": 0,
            "plate_bytes": 0,
            "required_tile_count": 0,
        }
        self.z_min, self.z_max = int(z_min), int(z_max)
        ext_w = 3 * self.W + 8
        ext_h = 3 * self.H + 8
        specs: dict[int, list[tuple[float, float, int, int]]] = {}
        required_tiles: set[tuple[int, int, int]] = set()

        # First plan every plate and union its exact tile footprint. This is
        # the same half-open tile range consumed below by _compose_px.
        for z in range(self.z_min, self.z_max + 1):
            pts = [latlon_to_global_px(lat, lon, z) for lat, lon in anchors]
            xs = [x for x, _ in pts]
            ys = [y for _, y in pts]
            w = int(math.ceil(max(xs) - min(xs))) + ext_w
            h = int(math.ceil(max(ys) - min(ys))) + ext_h
            if w <= self.MAX_PLATE_W and h <= self.MAX_PLATE_H:
                cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
                plate_specs = [(cx - w / 2, cy - h / 2, w, h)]
            else:
                plate_specs = [(ax - ext_w / 2, ay - ext_h / 2, ext_w, ext_h)
                               for ax, ay in pts]
            specs[z] = plate_specs
            world_tiles = 2 ** z
            for left, top, width, height in plate_specs:
                min_tx = math.floor(left / 256.0)
                max_tx = math.ceil((left + width) / 256.0)
                min_ty = max(0, math.floor(top / 256.0))
                max_ty = min(world_tiles, math.ceil((top + height) / 256.0))
                for tile_y in range(min_ty, max_ty):
                    for tile_x in range(min_tx, max_tx):
                        required_tiles.add((z, tile_x % world_tiles, tile_y))

        prefetch = self.pyr.prefetch_tiles(required_tiles)
        if prefetch["fetched"] + prefetch["memory_hits"] != len(required_tiles):
            raise RuntimeError("tile prefetch result does not cover the exact required plate set")
        self.prepare_telemetry["required_tile_count"] = len(required_tiles)
        self.prepare_telemetry["prefetch_ms"] = float(prefetch["elapsed_ms"])
        self.prepare_telemetry["prefetch_tiles_fetched"] = int(prefetch["fetched"])
        self.prepare_telemetry["prefetch_tiles_memory_hits"] = int(prefetch["memory_hits"])

        compose_started_all = time.perf_counter()
        for z, plate_specs in specs.items():
            entries = []
            for left, top, width, height in plate_specs:
                plate = self._compose_px(left, top, width, height, z)
                entries.append((left, top, plate))
                self.prepare_telemetry["plate_count"] += 1
                self.prepare_telemetry["plate_bytes"] += plate.nbytes
            if entries:
                self.plates[z] = entries
        self.prepare_telemetry["plate_compose_ms"] = (time.perf_counter() - compose_started_all) * 1000.0
        self.prepare_telemetry["prepare_ms"] = (time.perf_counter() - prepare_started) * 1000.0

    def can_sample(self, lat: float, lon: float, zoom: float,
                   width: int | None = None, height: int | None = None) -> bool:
        """Check crop coverage without rendering pixels or triggering tile I/O."""
        from dynamic_tile_pyramid import latlon_to_global_px

        W = self.W if width is None else int(width)
        H = self.H if height is None else int(height)
        z_lo = int(math.floor(zoom))
        if not (self.z_min <= z_lo <= self.z_max):
            return False
        t = zoom - z_lo
        w_lo = int(round(W / (2.0 ** t))) + 4
        h_lo = int(round(H / (2.0 ** t))) + 4
        if self._crop(z_lo, latlon_to_global_px(lat, lon, z_lo), w_lo, h_lo) is None:
            return False
        if t * t * (3.0 - 2.0 * t) <= 0.001 or z_lo + 1 > self.z_max:
            return True
        scale_hi = 2.0 ** (t - 1.0)
        w_hi = int(round(W / scale_hi)) + 4
        h_hi = int(round(H / scale_hi)) + 4
        return self._crop(z_lo + 1, latlon_to_global_px(lat, lon, z_lo + 1),
                          w_hi, h_hi) is not None

    def required_tile_keys(self) -> set[tuple[int, int, int]]:
        """Return exact Mercator tile coverage of every prepared raster plate."""
        keys = set()
        for zoom, entries in self.plates.items():
            world_tiles = 2 ** zoom
            for left, top, plate in entries:
                right = left + plate.shape[1]
                bottom = top + plate.shape[0]
                min_tx = math.floor(left / 256.0)
                max_tx = math.ceil(right / 256.0)
                min_ty = max(0, math.floor(top / 256.0))
                max_ty = min(world_tiles, math.ceil(bottom / 256.0))
                for tile_y in range(min_ty, max_ty):
                    for tile_x in range(min_tx, max_tx):
                        keys.add((zoom, tile_x % world_tiles, tile_y))
        return keys

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
                    self.pyr.record_telemetry("late_tile_fetches")
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

    def _sample_subpixel_level(self, z: int, anchor_px: tuple[float, float],
                               source_per_output_pixel: float,
                               width: int, height: int) -> np.ndarray | None:
        """Sample a prepared plate at continuous coordinates to avoid 1 px camera judder."""
        x_extent = width / (2.0 * source_per_output_pixel) + 2.0
        y_extent = height / (2.0 * source_per_output_pixel) + 2.0
        for origin_x, origin_y, plate in self.plates.get(z, ()):
            left = anchor_px[0] - x_extent
            top = anchor_px[1] - y_extent
            right = anchor_px[0] + x_extent
            bottom = anchor_px[1] + y_extent
            if (left < origin_x + 1 or top < origin_y + 1 or
                    right >= origin_x + plate.shape[1] - 1 or
                    bottom >= origin_y + plate.shape[0] - 1):
                continue

            out_x = np.arange(width, dtype=np.float32)[None, :]
            out_y = np.arange(height, dtype=np.float32)[:, None]
            map_x = np.broadcast_to(
                anchor_px[0] + (out_x - (width - 1) / 2.0) / source_per_output_pixel - origin_x,
                (height, width),
            ).copy()
            map_y = np.broadcast_to(
                anchor_px[1] + (out_y - (height - 1) / 2.0) / source_per_output_pixel - origin_y,
                (height, width),
            ).copy()
            return cv2.remap(plate, map_x, map_y, cv2.INTER_LINEAR,
                             borderMode=cv2.BORDER_REPLICATE)
        return None

    def sample_subpixel(self, lat: float, lon: float, zoom: float,
                        width: int | None = None,
                        height: int | None = None) -> np.ndarray:
        """Like sample(), but keep fractional camera movement through source remapping."""
        from dynamic_tile_pyramid import latlon_to_global_px

        W = self.W if width is None else int(width)
        H = self.H if height is None else int(height)
        z_lo = int(math.floor(zoom))
        t = zoom - z_lo
        alpha = t * t * (3.0 - 2.0 * t)
        if not (self.z_min <= z_lo <= self.z_max):
            return self.sample(lat, lon, zoom, W, H)

        lo = self._sample_subpixel_level(z_lo, latlon_to_global_px(lat, lon, z_lo),
                                         2.0 ** t, W, H)
        if lo is None:
            return self.sample(lat, lon, zoom, W, H)
        if alpha <= 0.001 or z_lo + 1 > self.z_max:
            return lo

        hi = self._sample_subpixel_level(z_lo + 1, latlon_to_global_px(lat, lon, z_lo + 1),
                                         2.0 ** (t - 1.0), W, H)
        if hi is None:
            return self.sample(lat, lon, zoom, W, H)
        return cv2.addWeighted(lo, 1.0 - alpha, hi, alpha, 0.0)

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
