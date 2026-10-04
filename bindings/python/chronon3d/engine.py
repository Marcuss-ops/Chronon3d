"""Pythonic wrapper over the Chronon3D C ABI."""

from __future__ import annotations

import ctypes
import json
import os
import threading
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Iterable, Iterator, Mapping, Union

from . import _lib
from .errors import CancelledError, ChrononError, Status, error_for

PlanSource = Union[str, bytes, os.PathLike, Mapping[str, Any]]
ProgressFn = Callable[[int, int], None]


def abi_version() -> int:
    return int(_lib.load().chronon_abi_version())


def contract_version() -> int:
    return int(_lib.load().chronon_contract_version())


def version() -> str:
    return _lib.load().chronon_version_string().decode()


def status_name(status: int) -> str:
    return _lib.load().chronon_status_name(int(status)).decode()


def _s(b: bytes | None) -> str | None:
    return b.decode(errors="replace") if b else None


@dataclass(frozen=True)
class Frame:
    """One rendered frame (pixels copied into Python-owned memory)."""

    index: int
    width: int
    height: int
    stride: int
    pixel_format: int
    data: bytes

    def to_pil(self):  # pragma: no cover - optional dependency
        """Return a ``PIL.Image`` (requires Pillow; RGBA8 frames only)."""
        from PIL import Image

        return Image.frombuffer("RGBA", (self.width, self.height), self.data, "raw", "RGBA", self.stride, 1)


class Plan:
    """A compiled RenderPlan. Owned by the engine that compiled it."""

    def __init__(self, engine: "RenderEngine", handle: int, canvas: Mapping[str, Any] | None):
        self._engine = engine
        self._handle: int | None = handle
        canvas = canvas or {}
        self.width = canvas.get("width")
        self.height = canvas.get("height")
        self.fps = (int(canvas.get("fps_num", 30)), int(canvas.get("fps_den", 1)))
        self.duration_frames = canvas.get("duration_frames")

    @property
    def handle(self) -> int:
        if self._handle is None:
            raise ChrononError("plan is closed", status=Status.INVALID_ARGUMENT)
        return self._handle

    def close(self) -> None:
        if self._handle is not None:
            _lib.load().chronon_plan_destroy(self._handle)
            self._handle = None
            try:
                self._engine._plans.remove(self)
            except ValueError:
                pass

    def __enter__(self) -> "Plan":
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def __del__(self) -> None:  # best-effort; prefer explicit close / with
        try:
            if self._engine._handle is not None:
                self.close()
        except Exception:
            pass


class RenderEngine:
    """A Chronon3D engine instance.

    Use as a context manager so native resources are released deterministically::

        with RenderEngine(assets_root="assets") as eng:
            frame = eng.render_still(plan_dict, frame=0)

    One engine serializes its renders (the C ABI returns ``BUSY`` on concurrent
    use); the wrapper holds a lock so calls from several Python threads queue
    instead of failing.
    """

    def __init__(self, assets_root: str | os.PathLike | None = None, *, library: str | None = None,
                 log: Callable[[int, str, str], None] | None = None):
        self._lib = _lib.load(library)
        self._lock = threading.RLock()
        self._handle: int | None = None
        self._plans: list[Plan] = []
        self._log_cb = None
        self._assets_root = os.fspath(assets_root) if assets_root is not None else None
        self._create()
        if log is not None:
            self.set_log_callback(log)

    # ── lifetime ────────────────────────────────────────────────────────
    def _create(self) -> None:
        cfg = _lib.EngineConfig()
        cfg.struct_size = ctypes.sizeof(cfg)
        cfg.abi_version = self._lib.chronon_abi_version()
        cfg.assets_root = self._assets_root.encode() if self._assets_root else None
        cfg.flags = 0
        err = _lib.ErrorInfo()
        err.struct_size = ctypes.sizeof(err)
        out = ctypes.c_void_p()
        st = self._lib.chronon_engine_create_v2(ctypes.byref(cfg), ctypes.byref(out), ctypes.byref(err))
        if st != 0 or not out.value:
            raise error_for(st, _s(err.message) or "engine creation failed", code=_s(err.code),
                            component=_s(err.component))
        self._handle = out.value

    def close(self) -> None:
        with self._lock:
            for p in self._plans:
                p.close()
            self._plans.clear()
            if self._handle is not None:
                self._lib.chronon_engine_destroy(self._handle)
                self._handle = None

    @property
    def closed(self) -> bool:
        return self._handle is None

    def __enter__(self) -> "RenderEngine":
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def __del__(self) -> None:
        try:
            self.close()
        except Exception:
            pass

    @property
    def handle(self) -> int:
        if self._handle is None:
            raise ChrononError("engine is closed", status=Status.INVALID_ARGUMENT)
        return self._handle

    # ── configuration ───────────────────────────────────────────────────
    @property
    def assets_root(self) -> str | None:
        return self._assets_root

    def set_assets_root(self, path: str | os.PathLike | None) -> None:
        """Change the assets root.

        The C ABI only accepts the assets root at engine creation, so this
        recreates the native engine. Previously compiled plans are invalidated
        (closed) because asset resolution happens at compile time.
        """
        with self._lock:
            self.close()
            self._assets_root = os.fspath(path) if path is not None else None
            self._create()
            if self._log_cb is not None:
                self._lib.chronon_engine_set_log_callback(self._handle, self._log_cb, None)

    def set_log_callback(self, fn: Callable[[int, str, str], None] | None) -> None:
        def tramp(level, component, message, _user):
            try:
                fn(int(level), _s(component) or "", _s(message) or "")
            except Exception:  # never propagate into C
                pass

        cb = _lib.LOG_FN(tramp) if fn else _lib.LOG_FN()
        self._check(self._lib.chronon_engine_set_log_callback(self.handle, cb, None))
        self._log_cb = cb if fn else None  # keep alive

    # ── errors ──────────────────────────────────────────────────────────
    def last_error(self) -> ChrononError | None:
        info = _lib.ErrorInfo()
        info.struct_size = ctypes.sizeof(info)
        if self._lib.chronon_engine_last_error_info(self.handle, ctypes.byref(info)) != 0 or info.status == 0:
            return None
        return error_for(info.status, _s(info.message) or "", code=_s(info.code),
                         component=_s(info.component), node_id=_s(info.node_id), asset=_s(info.asset))

    def _check(self, st: int) -> None:
        if st == 0:
            return
        err = self.last_error() if self._handle is not None else None
        if err is None or err.status != st:
            err = error_for(st, status_name(st))
        raise err

    # ── plans ───────────────────────────────────────────────────────────
    def compile(self, plan: PlanSource) -> Plan:
        """Compile a RenderPlan from a dict, JSON str/bytes, or a path to a .json file."""
        if isinstance(plan, Mapping):
            obj: Any = plan
            raw = json.dumps(plan).encode()
        elif isinstance(plan, (bytes, bytearray)):
            raw = bytes(plan)
            obj = None
        elif isinstance(plan, os.PathLike) or (isinstance(plan, str) and not plan.lstrip().startswith("{")):
            raw = Path(plan).read_bytes()
            obj = None
        elif isinstance(plan, str):
            raw = plan.encode()
            obj = None
        else:
            raise TypeError(f"unsupported plan source: {type(plan).__name__}")
        if obj is None:
            try:
                obj = json.loads(raw)
            except ValueError:
                obj = {}
        out = ctypes.c_void_p()
        with self._lock:
            self._check(self._lib.chronon_plan_compile_json_n(self.handle, raw, len(raw), ctypes.byref(out)))
            p = Plan(self, out.value, obj.get("canvas") if isinstance(obj, Mapping) else None)
            self._plans.append(p)
        return p

    def _as_plan(self, plan: Plan | PlanSource) -> tuple[Plan, bool]:
        return (plan, False) if isinstance(plan, Plan) else (self.compile(plan), True)

    # ── rendering ───────────────────────────────────────────────────────
    def render_still(self, plan: Plan | PlanSource, frame: int = 0) -> Frame:
        """Render one frame and return a :class:`Frame` with copied pixels."""
        p, owned = self._as_plan(plan)
        try:
            buf = _lib.FrameBuffer()
            with self._lock:
                self._check(self._lib.chronon_render_frame(self.handle, p.handle, int(frame), ctypes.byref(buf)))
                data = ctypes.string_at(buf.data, buf.size)
                return Frame(int(frame), buf.width, buf.height, buf.stride, buf.pixel_format, data)
        finally:
            if owned:
                p.close()

    def render_sequence(self, plan: Plan | PlanSource, frames: Iterable[int] | None = None) -> Iterator[Frame]:
        """Yield frames lazily. ``frames`` defaults to ``range(canvas.duration_frames)``."""
        p, owned = self._as_plan(plan)
        try:
            if frames is None:
                frames = range(int(p.duration_frames or 1))
            for i in frames:
                yield self.render_still(p, i)
        finally:
            if owned:
                p.close()

    def render_file(self, plan: Plan | PlanSource, output_path: str | os.PathLike, *,
                    start: int = 0, end: int | None = None, fps: tuple[int, int] | None = None,
                    progress: ProgressFn | None = None,
                    cancelled: Callable[[], bool] | threading.Event | None = None) -> Path:
        """Render frames ``[start, end]`` to a file (video or image, chosen by the engine from the extension).

        ``cancelled`` may be a callable or a ``threading.Event``; when it turns
        true the render stops and :class:`CancelledError` is raised.
        """
        p, owned = self._as_plan(plan)
        try:
            if end is None:
                end = max(int(p.duration_frames or 1) - 1, start)
            fps_num, fps_den = fps or p.fps
            exc: list[BaseException] = []

            def on_progress(_u, cur, total):
                if progress:
                    try:
                        progress(int(cur), int(total))
                    except BaseException as e:  # surfaced after the call
                        exc.append(e)

            def is_cancelled(_u):
                if exc:
                    return 1
                if cancelled is None:
                    return 0
                try:
                    return int(bool(cancelled.is_set() if isinstance(cancelled, threading.Event) else cancelled()))
                except BaseException as e:
                    exc.append(e)
                    return 1

            cbs = _lib.RenderCallbacks(_lib.PROGRESS_FN(on_progress), _lib.CANCELLED_FN(is_cancelled), None)
            out = os.fspath(output_path)
            with self._lock:
                st = self._lib.chronon_render_file(self.handle, p.handle, out.encode(), int(start), int(end),
                                                   int(fps_num), int(fps_den), ctypes.byref(cbs))
            if exc:
                raise exc[0]
            self._check(st)
            return Path(out)
        finally:
            if owned:
                p.close()

    # alias matching the "video" vocabulary
    render_video = render_file


__all__ = ["RenderEngine", "Plan", "Frame", "abi_version", "contract_version", "version",
           "status_name", "CancelledError"]
