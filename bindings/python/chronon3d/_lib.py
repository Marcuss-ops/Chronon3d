"""Low-level ctypes declarations mirroring ``<chronon/abi.h>`` (ABI major 2).

Only this module knows about struct layouts and symbol signatures; the public
wrapper in :mod:`chronon3d.engine` builds on it.
"""

from __future__ import annotations

import ctypes
import ctypes.util
import os
import shutil
import subprocess
import sys
from pathlib import Path

ABI_VERSION = 2

c_uint32 = ctypes.c_uint32
c_uint64 = ctypes.c_uint64
c_char_p = ctypes.c_char_p
c_void_p = ctypes.c_void_p


class EngineConfig(ctypes.Structure):
    _fields_ = [
        ("struct_size", c_uint32),
        ("abi_version", c_uint32),
        ("assets_root", c_char_p),
        ("flags", c_uint32),
    ]


class FrameBuffer(ctypes.Structure):
    _fields_ = [
        ("data", c_void_p),
        ("size", c_uint64),
        ("width", c_uint32),
        ("height", c_uint32),
        ("stride", c_uint32),
        ("pixel_format", c_uint32),
    ]


class FrameInfo(ctypes.Structure):
    _fields_ = [
        ("width", c_uint32),
        ("height", c_uint32),
        ("stride", c_uint32),
        ("pixel_format", c_uint32),
        ("size", c_uint64),
    ]


class ErrorInfo(ctypes.Structure):
    _fields_ = [
        ("struct_size", c_uint32),
        ("status", ctypes.c_int),
        ("message", c_char_p),
        ("code", c_char_p),
        ("component", c_char_p),
        ("node_id", c_char_p),
        ("asset", c_char_p),
    ]


PROGRESS_FN = ctypes.CFUNCTYPE(None, c_void_p, c_uint64, c_uint64)
CANCELLED_FN = ctypes.CFUNCTYPE(ctypes.c_int, c_void_p)
LOG_FN = ctypes.CFUNCTYPE(None, ctypes.c_int, c_char_p, c_char_p, c_void_p)


class RenderCallbacks(ctypes.Structure):
    _fields_ = [
        ("progress", PROGRESS_FN),
        ("is_cancelled", CANCELLED_FN),
        ("user", c_void_p),
    ]


_LIB_NAMES = {
    "linux": "libchronon3d_c.so",
    "darwin": "libchronon3d_c.dylib",
    "win32": "chronon3d_c.dll",
}


def _candidates() -> list[str]:
    out: list[str] = []
    env = os.environ.get("CHRONON3D_LIB")
    if env:
        p = Path(env)
        out.append(str(p / _LIB_NAMES.get(sys.platform, "libchronon3d_c.so")) if p.is_dir() else env)
    if shutil.which("pkg-config"):
        try:
            libdir = subprocess.run(
                ["pkg-config", "--variable=libdir", "chronon3d"],
                capture_output=True, text=True, check=True, timeout=5,
            ).stdout.strip()
            if libdir:
                out.append(str(Path(libdir) / _LIB_NAMES.get(sys.platform, "libchronon3d_c.so")))
        except (subprocess.SubprocessError, OSError):
            pass
    found = ctypes.util.find_library("chronon3d_c")
    if found:
        out.append(found)
    return out


def find_library() -> str:
    """Resolve the shared library: ``CHRONON3D_LIB`` > pkg-config > system search."""
    for cand in _candidates():
        if os.path.isabs(cand) and not os.path.exists(cand):
            continue
        return cand
    raise OSError(
        "libchronon3d_c not found: set CHRONON3D_LIB to the library path, "
        "or install Chronon3D so `pkg-config chronon3d` resolves"
    )


def _declare(lib: ctypes.CDLL) -> ctypes.CDLL:
    P = c_void_p
    sigs = {
        "chronon_abi_version": (c_uint32, []),
        "chronon_contract_version": (c_uint32, []),
        "chronon_version_string": (c_char_p, []),
        "chronon_status_name": (c_char_p, [ctypes.c_int]),
        "chronon_engine_create_v2": (ctypes.c_int, [ctypes.POINTER(EngineConfig), ctypes.POINTER(P), ctypes.POINTER(ErrorInfo)]),
        "chronon_engine_destroy": (None, [P]),
        "chronon_engine_last_error": (c_char_p, [P]),
        "chronon_engine_last_error_info": (ctypes.c_int, [P, ctypes.POINTER(ErrorInfo)]),
        "chronon_engine_set_log_callback": (ctypes.c_int, [P, LOG_FN, P]),
        "chronon_plan_compile_json_n": (ctypes.c_int, [P, c_char_p, c_uint64, ctypes.POINTER(P)]),
        "chronon_plan_destroy": (None, [P]),
        "chronon_render_frame": (ctypes.c_int, [P, P, c_uint64, ctypes.POINTER(FrameBuffer)]),
        "chronon_render_frame_into": (ctypes.c_int, [P, P, c_uint64, P, c_uint64, ctypes.POINTER(FrameInfo)]),
        "chronon_render_file": (ctypes.c_int, [P, P, c_char_p, c_uint64, c_uint64, c_uint32, c_uint32, ctypes.POINTER(RenderCallbacks)]),
        "chronon_buffer_free": (None, [P, ctypes.POINTER(FrameBuffer)]),
    }
    for name, (res, args) in sigs.items():
        fn = getattr(lib, name)
        fn.restype = res
        fn.argtypes = args
    return lib


_lib: ctypes.CDLL | None = None


def load(path: str | None = None) -> ctypes.CDLL:
    """Load (once) and return the declared C library."""
    global _lib
    if _lib is None or path is not None:
        lib = _declare(ctypes.CDLL(path or find_library()))
        if lib.chronon_abi_version() != ABI_VERSION:
            raise OSError(
                f"Chronon3D ABI mismatch: binding expects {ABI_VERSION}, "
                f"library reports {lib.chronon_abi_version()}"
            )
        _lib = lib
    return _lib
