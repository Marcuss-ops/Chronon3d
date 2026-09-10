#!/usr/bin/env python3
# sdk_consumers/05_python/probe.py
#
# Canonical Python C-ABI probe. Standard library only (ctypes), consumes ONLY
# the installed package — never the source tree.
#
# Usage: probe.py <libchronon3d_c.so> <abi|compile|schema|consume>
#
# Subcommands:
#   abi      chronon_abi_version() == 2                     -> ABI_VERSION_OK
#   compile  valid plan JSON compiles                       -> COMPILE_PLAN_OK
#   schema   invalid plan JSON is rejected                  -> SCHEMA_REJECT_OK
#   consume  full consumer flow (compile + render + assert) -> PYTHON_CONSUMER_PASS
#
# This file is the single authority for the Python ctypes surface of the C ABI;
# tools/verify_sdk_product.sh drives it directly (checks 6, 7, 12) instead of
# carrying an embedded copy.

import ctypes
import sys

CHRONON_OK = 0
CHRONON_ERROR_BUFFER_TOO_SMALL = 9

VALID_PLAN = (
    b'{"schema":"chronon.render-plan","version":1,'
    b'"canvas":{"width":8,"height":8,"fps":30,"duration_frames":1},'
    b'"layers":[{"id":"bg","type":"color","color":[0.2,0.4,0.6,1.0]}],'
    b'"output":{"path":"out.png"}}'
)
INVALID_PLAN = (
    b'{"schema":"chronon.render-plan","version":1,'
    b'"layers":[],"output":{"path":"out.png"}}'
)


class EngineConfig(ctypes.Structure):
    _fields_ = [("struct_size", ctypes.c_uint32),
                ("abi_version", ctypes.c_uint32),
                ("assets_root", ctypes.c_char_p),
                ("flags", ctypes.c_uint32)]


class ErrorInfo(ctypes.Structure):
    _fields_ = [("struct_size", ctypes.c_uint32),
                ("status", ctypes.c_int),
                ("message", ctypes.c_char_p),
                ("code", ctypes.c_char_p),
                ("component", ctypes.c_char_p),
                ("node_id", ctypes.c_char_p),
                ("asset", ctypes.c_char_p)]


class FrameInfo(ctypes.Structure):
    _fields_ = [("width", ctypes.c_uint32),
                ("height", ctypes.c_uint32),
                ("stride", ctypes.c_uint32),
                ("pixel_format", ctypes.c_uint32),
                ("size", ctypes.c_uint64)]


def load(lib):
    c = ctypes.CDLL(lib)
    c.chronon_abi_version.restype = ctypes.c_uint32
    c.chronon_engine_create_v2.argtypes = [
        ctypes.POINTER(EngineConfig),
        ctypes.POINTER(ctypes.c_void_p),
        ctypes.POINTER(ErrorInfo),
    ]
    c.chronon_engine_create_v2.restype = ctypes.c_int
    c.chronon_plan_compile_json_n.argtypes = [
        ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint64,
        ctypes.POINTER(ctypes.c_void_p),
    ]
    c.chronon_plan_compile_json_n.restype = ctypes.c_int
    c.chronon_render_frame_into.argtypes = [
        ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint64,
        ctypes.c_void_p, ctypes.c_uint64, ctypes.POINTER(FrameInfo),
    ]
    c.chronon_render_frame_into.restype = ctypes.c_int
    c.chronon_plan_destroy.argtypes = [ctypes.c_void_p]
    c.chronon_engine_destroy.argtypes = [ctypes.c_void_p]
    return c


def make_engine(c):
    cfg = EngineConfig(ctypes.sizeof(EngineConfig), c.chronon_abi_version(), None, 0)
    err = ErrorInfo(ctypes.sizeof(ErrorInfo), 0, None, None, None, None, None)
    engine = ctypes.c_void_p()
    if c.chronon_engine_create_v2(ctypes.byref(cfg), ctypes.byref(engine),
                                  ctypes.byref(err)) != CHRONON_OK or not engine:
        raise RuntimeError("engine create failed: %s"
                           % (err.message.decode() if err.message else "no error"))
    return engine


def compile_plan(c, engine, source):
    plan = ctypes.c_void_p()
    status = c.chronon_plan_compile_json_n(engine, source, len(source),
                                           ctypes.byref(plan))
    return status, plan


def cmd_abi(c):
    version = c.chronon_abi_version()
    if version != 2:
        print("ABI_VERSION_MISMATCH: %d" % version)
        return 1
    print("ABI_VERSION_OK: 2")
    return 0


def cmd_compile(c):
    engine = make_engine(c)
    try:
        status, plan = compile_plan(c, engine, VALID_PLAN)
        if status != CHRONON_OK or not plan:
            print("COMPILE_PLAN_FAILED: status=%d" % status)
            return 1
        print("COMPILE_PLAN_OK")
        return 0
    finally:
        c.chronon_engine_destroy(engine)


def cmd_schema(c):
    engine = make_engine(c)
    try:
        status, plan = compile_plan(c, engine, INVALID_PLAN)
        if status == CHRONON_OK:
            print("SCHEMA_REJECT_FAILED: invalid plan was accepted")
            return 1
        print("SCHEMA_REJECT_OK: status=%d" % status)
        return 0
    finally:
        c.chronon_engine_destroy(engine)


def cmd_consume(c):
    engine = make_engine(c)
    plan = ctypes.c_void_p()
    try:
        status, plan = compile_plan(c, engine, VALID_PLAN)
        if status != CHRONON_OK or not plan:
            print("PYTHON_CONSUMER_FAILED: compile status=%d" % status)
            return 1
        info = FrameInfo()
        status = c.chronon_render_frame_into(engine, plan, 0, None, 0,
                                             ctypes.byref(info))
        if status != CHRONON_ERROR_BUFFER_TOO_SMALL or info.size == 0:
            print("PYTHON_CONSUMER_FAILED: size query status=%d" % status)
            return 1
        buf = ctypes.create_string_buffer(info.size)
        status = c.chronon_render_frame_into(engine, plan, 0, buf, info.size,
                                             ctypes.byref(info))
        if status != CHRONON_OK:
            print("PYTHON_CONSUMER_FAILED: render status=%d" % status)
            return 1
        if not any(buf.raw):
            print("PYTHON_CONSUMER_FAILED: rendered frame is empty")
            return 1
        print("PYTHON_CONSUMER_PASS %dx%d" % (info.width, info.height))
        return 0
    finally:
        c.chronon_plan_destroy(plan)
        c.chronon_engine_destroy(engine)


def main():
    if len(sys.argv) != 3:
        print("usage: probe.py <libchronon3d_c.so> <abi|compile|schema|consume>")
        return 2
    lib, sub = sys.argv[1], sys.argv[2]
    c = load(lib)
    if sub == "abi":
        return cmd_abi(c)
    if sub == "compile":
        return cmd_compile(c)
    if sub == "schema":
        return cmd_schema(c)
    if sub == "consume":
        return cmd_consume(c)
    print("unknown subcommand: %s" % sub)
    return 2


if __name__ == "__main__":
    sys.exit(main())
