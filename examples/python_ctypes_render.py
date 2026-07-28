import ctypes
import os
import sys

lib_path = "./build/chronon/linux-release/src/libchronon3d_c.so"
if not os.path.exists(lib_path):
    print(f"Error: Shared library not found at {lib_path}")
    sys.exit(1)

chronon = ctypes.CDLL(lib_path)
chronon.chronon_abi_version.restype = ctypes.c_uint32
chronon.chronon_version_string.restype = ctypes.c_char_p

class EngineConfig(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("abi_version", ctypes.c_uint32),
        ("assets_root", ctypes.c_char_p),
        ("flags", ctypes.c_uint32),
    ]

class FrameBuffer(ctypes.Structure):
    _fields_ = [
        ("data", ctypes.c_void_p),
        ("size", ctypes.c_uint64),
        ("width", ctypes.c_uint32),
        ("height", ctypes.c_uint32),
        ("stride", ctypes.c_uint32),
        ("pixel_format", ctypes.c_uint32),
    ]

chronon.chronon_engine_create.argtypes = [ctypes.POINTER(EngineConfig)]
chronon.chronon_engine_create.restype = ctypes.c_void_p
chronon.chronon_engine_last_error.argtypes = [ctypes.c_void_p]
chronon.chronon_engine_last_error.restype = ctypes.c_char_p
chronon.chronon_plan_compile_json.argtypes = [
    ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p)]
chronon.chronon_plan_compile_json.restype = ctypes.c_int
chronon.chronon_render_frame.argtypes = [
    ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint64, ctypes.POINTER(FrameBuffer)]
chronon.chronon_render_frame.restype = ctypes.c_int
chronon.chronon_plan_destroy.argtypes = [ctypes.c_void_p]
chronon.chronon_engine_destroy.argtypes = [ctypes.c_void_p]

print(f"Loaded Chronon3D C ABI Version: {chronon.chronon_version_string().decode()}")
config = EngineConfig(ctypes.sizeof(EngineConfig), chronon.chronon_abi_version(), None, 0)
engine = chronon.chronon_engine_create(ctypes.byref(config))
if not engine:
    print("Failed to create engine (ABI/config mismatch)")
    sys.exit(1)

plan_json = b'''{
  "schema": "chronon.render-plan", "version": 1,
  "canvas": {"width": 1280, "height": 720, "fps": 30, "duration_frames": 60},
  "layers": [{"id": "green_rect", "type": "color", "color": [0.1, 0.8, 0.4, 1.0]}],
  "output": {"path": "output_python.ppm", "format": "png"}
}'''

plan = ctypes.c_void_p()
status = chronon.chronon_plan_compile_json(engine, plan_json, ctypes.byref(plan))
if status != 0:
    print("Plan compilation failed:", chronon.chronon_engine_last_error(engine).decode())
    chronon.chronon_engine_destroy(engine)
    sys.exit(1)

buffer = FrameBuffer()
status = chronon.chronon_render_frame(engine, plan, 0, ctypes.byref(buffer))
if status != 0:
    print("Render failed:", chronon.chronon_engine_last_error(engine).decode())
else:
    pixels = ctypes.string_at(buffer.data, buffer.size)
    with open("output_python.ppm", "wb") as output:
        output.write(f"P6\n{buffer.width} {buffer.height}\n255\n".encode())
        for y in range(buffer.height):
            row = pixels[y * buffer.stride:(y + 1) * buffer.stride]
            output.write(b"".join(row[offset:offset + 3]
                                  for offset in range(0, buffer.width * 4, 4)))
    print("Render succeeded! Saved output_python.ppm")

chronon.chronon_plan_destroy(plan)
chronon.chronon_engine_destroy(engine)
