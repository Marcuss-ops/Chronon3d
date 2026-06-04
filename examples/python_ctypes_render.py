import ctypes
import os
import sys

# Load shared library
lib_path = "./build/chronon/linux-release/src/libchronon3d.so"
if not os.path.exists(lib_path):
    print(f"Error: Shared library not found at {lib_path}")
    sys.exit(1)

chronon = ctypes.CDLL(lib_path)

# Set return types
chronon.chronon_version_string.restype = ctypes.c_char_p
chronon.chronon_last_error.argtypes = [ctypes.c_void_p]
chronon.chronon_last_error.restype = ctypes.c_char_p

print(f"Loaded Chronon3D C ABI Version: {chronon.chronon_version_string().decode()}")

# Create context
ctx = chronon.chronon_create_context()
if not ctx:
    print("Failed to create context")
    sys.exit(1)

# Options structure
class ChrononRenderOptions(ctypes.Structure):
    _fields_ = [
        ("width", ctypes.c_uint32),
        ("height", ctypes.c_uint32),
        ("frame", ctypes.c_uint32),
        ("fps", ctypes.c_uint32),
        ("flags", ctypes.c_uint32)
    ]

# JSON Scene
json_scene = """
{
  "name": "Python CTypes Test",
  "width": 1280,
  "height": 720,
  "duration": 60,
  "layers": [
    {
      "id": "green_rect",
      "visuals": [
        {
          "type": "rect",
          "size": [400.0, 300.0],
          "color": [0.1, 0.8, 0.4, 1.0],
          "pos": [0.0, 0.0, 0.0]
        }
      ]
    }
  ]
}
"""

options = ChrononRenderOptions(1280, 720, 0, 30, 0)

print("Rendering scene from Python ctypes...")
status = chronon.chronon_render_json_string(
    ctx,
    json_scene.encode('utf-8'),
    b"output_python.png",
    ctypes.byref(options)
)

if status == 0:
    print("Render succeeded! Saved output_python.png")
else:
    error_msg = chronon.chronon_last_error(ctx).decode()
    print(f"Render failed with status {status}. Error: {error_msg}")

chronon.chronon_destroy_context(ctx)
