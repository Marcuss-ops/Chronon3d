#!/usr/bin/env python3
# sdk_consumers/05_python/main.py
#
# Minimal Python consumer.  Binds the C ABI with ctypes (no third-party
# packages) and loads libchronon3d_c.so from the installed prefix.

import ctypes
import os
import sys

CHRONON_OK = 0
CHRONON_ERROR_BUFFER_TOO_SMALL = 9


def resolve_library():
    prefix = os.environ.get("CHRONON3D_PREFIX") or os.environ.get("SDK_PREFIX") or "/usr/local"
    candidate = os.path.join(prefix, "lib", "libchronon3d_c.so")
    if os.path.exists(candidate):
        return candidate
    # Fall back to the loader search path (e.g. via LD_LIBRARY_PATH).
    return "libchronon3d_c.so"


class EngineConfig(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("abi_version", ctypes.c_uint32),
        ("assets_root", ctypes.c_char_p),
        ("flags", ctypes.c_uint32),
    ]


class ErrorInfo(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("status", ctypes.c_int),
        ("message", ctypes.c_char_p),
        ("code", ctypes.c_char_p),
        ("component", ctypes.c_char_p),
        ("node_id", ctypes.c_char_p),
        ("asset", ctypes.c_char_p),
    ]


class FrameInfo(ctypes.Structure):
    _fields_ = [
        ("width", ctypes.c_uint32),
        ("height", ctypes.c_uint32),
        ("stride", ctypes.c_uint32),
        ("pixel_format", ctypes.c_uint32),
        ("size", ctypes.c_uint64),
    ]


def main():
    lib = ctypes.CDLL(resolve_library())
    lib.chronon_abi_version.restype = ctypes.c_uint32
    lib.chronon_status_name.restype = ctypes.c_char_p
    lib.chronon_engine_create_v2.argtypes = [
        ctypes.POINTER(EngineConfig),
        ctypes.POINTER(ctypes.c_void_p),
        ctypes.POINTER(ErrorInfo),
    ]
    lib.chronon_engine_create_v2.restype = ctypes.c_int
    lib.chronon_plan_compile_json_n.argtypes = [
        ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint64,
        ctypes.POINTER(ctypes.c_void_p),
    ]
    lib.chronon_plan_compile_json_n.restype = ctypes.c_int
    lib.chronon_render_frame_into.argtypes = [
        ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint64,
        ctypes.c_void_p, ctypes.c_uint64, ctypes.POINTER(FrameInfo),
    ]
    lib.chronon_render_frame_into.restype = ctypes.c_int
    lib.chronon_plan_destroy.argtypes = [ctypes.c_void_p]
    lib.chronon_engine_destroy.argtypes = [ctypes.c_void_p]

    with open("plan.json", "rb") as fh:
        plan_json = fh.read()

    cfg = EngineConfig(ctypes.sizeof(EngineConfig), lib.chronon_abi_version(), None, 0)
    err = ErrorInfo(ctypes.sizeof(ErrorInfo), 0, None, None, None, None, None)
    engine = ctypes.c_void_p()
    status = lib.chronon_engine_create_v2(
        ctypes.byref(cfg), ctypes.byref(engine), ctypes.byref(err))
    if status != CHRONON_OK or not engine:
        print("engine create failed:", err.message.decode() if err.message else "no error",
              file=sys.stderr)
        return 1

    plan = ctypes.c_void_p()
    status = lib.chronon_plan_compile_json_n(
        engine, plan_json, len(plan_json), ctypes.byref(plan))
    if status != CHRONON_OK or not plan:
        print("plan compile failed:", status, file=sys.stderr)
        lib.chronon_engine_destroy(engine)
        return 1

    info = FrameInfo()
    status = lib.chronon_render_frame_into(engine, plan, 0, None, 0, ctypes.byref(info))
    if status != CHRONON_ERROR_BUFFER_TOO_SMALL or info.size == 0:
        print("size query failed:", status, file=sys.stderr)
        lib.chronon_plan_destroy(plan)
        lib.chronon_engine_destroy(engine)
        return 1

    buf = ctypes.create_string_buffer(info.size)
    status = lib.chronon_render_frame_into(engine, plan, 0, buf, info.size, ctypes.byref(info))
    if status != CHRONON_OK:
        print("render failed:", status, file=sys.stderr)
        lib.chronon_plan_destroy(plan)
        lib.chronon_engine_destroy(engine)
        return 1

    if not any(buf.raw):
        print("rendered frame is empty", file=sys.stderr)
        lib.chronon_plan_destroy(plan)
        lib.chronon_engine_destroy(engine)
        return 1

    lib.chronon_plan_destroy(plan)
    lib.chronon_engine_destroy(engine)
    print(f"PYTHON_CONSUMER_PASS {info.width}x{info.height}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
