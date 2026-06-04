#!/usr/bin/env python3
"""
Chronon3D Python Integration Utility.
Provides simple hooks to invoke Chronon3D rendering from external programs
via both CLI subprocess (for full MP4 video generation) and ctypes C-API (for raw frame rendering).
"""

import os
import sys
import ctypes
import subprocess
from typing import Optional, Union

# Set default library search paths
WORK_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLI_PATH = os.path.join(WORK_DIR, "build/chronon/linux-release-full/apps/chronon3d_cli/chronon3d_cli")
LIB_PATH = os.path.join(WORK_DIR, "build/chronon/linux-release-full/src/libchronon3d.so")

class ChrononRenderOptions(ctypes.Structure):
    _fields_ = [
        ("width", ctypes.c_uint32),
        ("height", ctypes.c_uint32),
        ("frame", ctypes.c_uint32),
        ("fps", ctypes.c_uint32),
        ("flags", ctypes.c_uint32)
    ]

class ChrononAPI:
    """Wrapper class around the native libchronon3d shared library."""
    def __init__(self, lib_path: str = LIB_PATH):
        if not os.path.exists(lib_path):
            raise FileNotFoundError(f"Shared library not found at: {lib_path}. Please compile the 'linux-release-full' preset.")
        self.lib = ctypes.CDLL(lib_path)
        
        # Define argtypes and restypes
        self.lib.chronon_create_context.restype = ctypes.c_void_p
        self.lib.chronon_destroy_context.argtypes = [ctypes.c_void_p]
        
        self.lib.chronon_render_json_file.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.POINTER(ChrononRenderOptions)
        ]
        self.lib.chronon_render_json_file.restype = ctypes.c_int
        
        self.lib.chronon_render_json_string.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.POINTER(ChrononRenderOptions)
        ]
        self.lib.chronon_render_json_string.restype = ctypes.c_int
        
        self.lib.chronon_last_error.argtypes = [ctypes.c_void_p]
        self.lib.chronon_last_error.restype = ctypes.c_char_p
        
        self.lib.chronon_version_string.restype = ctypes.c_char_p

    def version(self) -> str:
        return self.lib.chronon_version_string().decode()


def render_video_cli(
    composition_id: str,
    output_path: str,
    cli_bin: str = CLI_PATH,
    dirty_rects: bool = True,
    graph: bool = True,
    crf: int = 23,
    preset: str = "ultrafast",
    chunks: int = 1,
    ffmpeg_mode: str = "pipe"
) -> bool:
    """
    Invokes Chronon3D CLI tool to render a full video animation.
    
    Args:
        composition_id: Name of registered composition (e.g. 'MinimalistImageElegantExit') or path to .specscene TOML/JSON.
        output_path: Target video file path (should end in .mp4).
        cli_bin: Path to compiled chronon3d_cli binary.
        dirty_rects: True to use dirty rects optimizations (recommended for speed).
        graph: True to use the modular render graph pipeline.
        crf: Encoding CRF (0-51, lower means higher quality).
        preset: FFmpeg compression preset (e.g., ultrafast, superfast, fast, medium).
        chunks: Number of parallel chunks to render (only applies if ffmpeg_mode='png').
        ffmpeg_mode: 'pipe' for in-memory streaming, 'png' for multi-process chunks rendering.
    
    Returns:
        True if rendering succeeded, False otherwise.
    """
    if not os.path.exists(cli_bin):
        print(f"Error: CLI binary not found at: {cli_bin}. Run cmake build with release-full preset first.", file=sys.stderr)
        return False

    cmd = [
        cli_bin,
        "video",
        composition_id,
        "-o", output_path,
        "--crf", str(crf),
        "--preset", preset,
        "--ffmpeg-mode", ffmpeg_mode
    ]

    if dirty_rects:
        cmd.append("--dirty-rects")
    if graph:
        cmd.append("--graph")
    if ffmpeg_mode == "png" and chunks > 1:
        cmd.extend(["--chunks", str(chunks)])
        cmd.extend(["--encoder-backend", "pipe"])

    print(f"Invoking CLI: {' '.join(cmd)}")
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    
    if result.returncode == 0:
        print(f"Success! Video rendered at: {output_path}")
        return True
    else:
        print(f"Error rendering video via CLI. Return code: {result.returncode}", file=sys.stderr)
        print(f"Stdout:\n{result.stdout}", file=sys.stderr)
        print(f"Stderr:\n{result.stderr}", file=sys.stderr)
        return False


def render_frame_c_api(
    scene_json_or_file: str,
    output_png_path: str,
    frame: int = 0,
    width: int = 1920,
    height: int = 1920,
    fps: int = 30,
    lib_path: str = LIB_PATH
) -> bool:
    """
    Uses the Shared Library C-API to render a single frame directly in-process.
    
    Args:
        scene_json_or_file: Raw JSON string or path to a JSON/TOML scene definition.
        output_png_path: Target path for the output PNG.
        frame: Specific frame index to render.
        width: Render viewport width.
        height: Render viewport height.
        fps: Compositions frame rate.
        lib_path: Path to libchronon3d.so.
        
    Returns:
        True if rendering succeeded, False otherwise.
    """
    try:
        api = ChrononAPI(lib_path)
    except Exception as e:
        print(f"Error initializing Chronon3D C-API: {e}", file=sys.stderr)
        return False
        
    ctx = api.lib.chronon_create_context()
    if not ctx:
        print("Error: Could not create Chronon3D API context.", file=sys.stderr)
        return False
        
    opts = ChrononRenderOptions(
        width=width,
        height=height,
        frame=frame,
        fps=fps,
        flags=0
    )
    
    # Determine if argument is a file or a string
    is_file = os.path.exists(scene_json_or_file) and not scene_json_or_file.strip().startswith("{")
    
    if is_file:
        status = api.lib.chronon_render_json_file(
            ctx,
            scene_json_or_file.encode("utf-8"),
            output_png_path.encode("utf-8"),
            ctypes.byref(opts)
        )
    else:
        status = api.lib.chronon_render_json_string(
            ctx,
            scene_json_or_file.encode("utf-8"),
            output_png_path.encode("utf-8"),
            ctypes.byref(opts)
        )
        
    success = (status == 0)
    if not success:
        err = api.lib.chronon_last_error(ctx).decode()
        print(f"Error rendering frame (status code {status}): {err}", file=sys.stderr)
        
    api.lib.chronon_destroy_context(ctx)
    return success


if __name__ == "__main__":
    # Example execution: Render the custom minimalist elegant exit to MP4
    output_file = os.path.join(WORK_DIR, "output/minimalist_image_elegant_exit.mp4")
    success = render_video_cli("MinimalistImageElegantExit", output_file, dirty_rects=True)
    if success:
        print("Verification run successful.")
    else:
        sys.exit(1)
