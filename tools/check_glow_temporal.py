#!/usr/bin/env python3
"""
check_glow_temporal.py — Glow temporal acceptance: frame sweep + MP4 SSIM.

Sub-commands:

  python3 tools/check_glow_temporal.py frames <frames_dir/>
    → Checks all frame_*.png files in the directory:
        - Exactly 60 frames
        - No empty frame (at least 1 visible pixel with alpha > 8)
        - No glow pop: inter-frame mean luma delta < 25.0
        - Pulse peak: frame 15 mean luma > frame 0 AND > frame 30
      Exit 0 = PASS.

  python3 tools/check_glow_temporal.py ssim <glow.mp4>
    → Decodes glow.mp4 to 60 PNG frames, then runs ffmpeg SSIM
      comparison between raw frames (frames_dir/) and decoded frames.
      Requires: ffmpeg with libx264 + SSIM filter.
      Exit 0 = PASS (SSIM mean >= 0.97).

TICKET-GLOW-CERTIFICATION — Azione 2 (atomic chore commit on main).
Per AGENTS.md §honesty: machine-verification deferred to working build host
with chronon3d_cli binary + ffmpeg.
"""

from pathlib import Path
from PIL import Image
import subprocess
import sys
import tempfile


# ── Luminance ──────────────────────────────────────────────────────────────

def luminance(rgb: tuple[int, int, int]) -> float:
    r, g, b = rgb
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def mean_luma(image: Image.Image) -> float:
    """Mean luminance of all visible pixels (alpha > 8)."""
    pixels = image.convert("RGBA")
    values: list[float] = []
    for r, g, b, a in pixels.getdata():
        if a > 8:
            values.append(luminance((r, g, b)))
    if not values:
        return 0.0
    return sum(values) / len(values)


# ── Sub-command: frames ────────────────────────────────────────────────────

def cmd_frames(frames_dir: str) -> int:
    folder = Path(frames_dir)
    if not folder.is_dir():
        print(f"ERROR: not a directory: {frames_dir}", file=sys.stderr)
        return 2

    frames = sorted(folder.glob("frame_*.png"))
    if len(frames) != 60:
        print(f"FAIL: expected 60 frames, found {len(frames)}", file=sys.stderr)
        return 1

    luma_values: list[float] = []
    for i, path in enumerate(frames):
        img = Image.open(path).convert("RGBA")
        ml = mean_luma(img)
        luma_values.append(ml)

        # No empty frame
        if ml < 0.001:
            print(f"FAIL: frame {i} is completely empty ({path})", file=sys.stderr)
            return 1

    # No glow pop between consecutive frames
    for i in range(1, len(luma_values)):
        jump = abs(luma_values[i] - luma_values[i - 1])
        if jump >= 25.0:
            print(f"FAIL: frame {i}: glow pop detected, delta={jump:.3f} "
                  f"(prev={luma_values[i-1]:.3f}, curr={luma_values[i]:.3f})",
                  file=sys.stderr)
            return 1

    # Pulse peak at frame 15
    if not (luma_values[15] > luma_values[0] and luma_values[15] > luma_values[30]):
        print(f"FAIL: pulse peak not at frame 15 "
              f"(f0={luma_values[0]:.3f}, f15={luma_values[15]:.3f}, "
              f"f30={luma_values[30]:.3f})", file=sys.stderr)
        return 1

    print(f"PASS: {len(frames)} frames — no empty, no pop, pulse peak at f15 "
          f"(f0={luma_values[0]:.2f}, f15={luma_values[15]:.2f}, "
          f"f30={luma_values[30]:.2f})")
    return 0


# ── Sub-command: ssim ──────────────────────────────────────────────────────

def cmd_ssim(mp4_path: str, frames_dir: str = "") -> int:
    mp4 = Path(mp4_path)
    if not mp4.is_file():
        print(f"ERROR: MP4 not found: {mp4_path}", file=sys.stderr)
        return 2

    # Auto-detect frames dir: same parent as mp4, subdir "frames"
    if not frames_dir:
        frames_dir = str(mp4.parent / "frames")

    raw_frames = Path(frames_dir)
    if not raw_frames.is_dir():
        print(f"ERROR: raw frames directory not found: {frames_dir}", file=sys.stderr)
        print("  Render first: chronon3d_cli video ChrononGlowFinalAE "
              "--ffmpeg-mode png --keep-frames --frames-dir <dir>", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory() as tmpdir:
        decoded_dir = Path(tmpdir) / "decoded"
        decoded_dir.mkdir()

        # Decode MP4 to PNG frames
        result = subprocess.run(
            ["ffmpeg", "-v", "error", "-i", str(mp4),
             "-vsync", "0", str(decoded_dir / "frame_%06d.png")],
            capture_output=True, text=True, timeout=120)
        if result.returncode != 0:
            print(f"FAIL: ffmpeg decode error: {result.stderr}", file=sys.stderr)
            return 1

        decoded = sorted(decoded_dir.glob("frame_*.png"))
        if len(decoded) != 60:
            print(f"FAIL: expected 60 decoded frames, got {len(decoded)}",
                  file=sys.stderr)
            return 1

        # SSIM comparison: raw PNGs vs decoded PNGs
        ssim_log = str(Path(tmpdir) / "ssim.log")
        ssim_result = subprocess.run(
            ["ffmpeg", "-framerate", "30",
             "-i", f"{frames_dir}/frame_%06d.png",
             "-framerate", "30",
             "-i", str(decoded_dir / "frame_%06d.png"),
             "-lavfi", f"ssim=stats_file={ssim_log}",
             "-f", "null", "-"],
            capture_output=True, text=True, timeout=120)

        # Parse SSIM log for mean value
        ssim_values: list[float] = []
        log_text = Path(ssim_log).read_text() if Path(ssim_log).exists() else ""
        for line in log_text.strip().split("\n"):
            if line.startswith("n:") or not line.strip():
                continue
            # Format: n:1 Y:0.987654 U:0.991234 V:0.992345 All:0.989012 (float)
            parts = line.split()
            for part in parts:
                if part.startswith("All:"):
                    try:
                        ssim_values.append(float(part.split(":")[1]))
                    except (ValueError, IndexError):
                        pass

        if not ssim_values:
            print("FAIL: could not parse SSIM values from ffmpeg output",
                  file=sys.stderr)
            print(f"  SSIM stderr: {ssim_result.stderr}", file=sys.stderr)
            return 1

        mean_ssim = sum(ssim_values) / len(ssim_values)
        min_ssim = min(ssim_values)

        print(f"SSIM: mean={mean_ssim:.6f}  min={min_ssim:.6f}  "
              f"frames={len(ssim_values)}")

        if mean_ssim < 0.97:
            print(f"FAIL: SSIM mean {mean_ssim:.6f} < 0.97", file=sys.stderr)
            return 1

        print(f"PASS: SSIM mean >= 0.97 ({mean_ssim:.6f}) — {len(decoded)} "
              f"decoded frames, {len(ssim_values)} SSIM samples")
        return 0


# ── Main ───────────────────────────────────────────────────────────────────

def main() -> int:
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} frames <dir/> | ssim <glow.mp4> [frames_dir/]",
              file=sys.stderr)
        return 2

    cmd = sys.argv[1]

    if cmd == "frames":
        if len(sys.argv) < 3:
            print(f"Usage: {sys.argv[0]} frames <frames_dir/>", file=sys.stderr)
            return 2
        return cmd_frames(sys.argv[2])

    elif cmd == "ssim":
        mp4 = sys.argv[2] if len(sys.argv) > 2 else ""
        frames_dir = sys.argv[3] if len(sys.argv) > 3 else ""
        return cmd_ssim(mp4, frames_dir)

    else:
        print(f"ERROR: unknown sub-command '{cmd}'.  Use 'frames' or 'ssim'.",
              file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
