#!/usr/bin/env python3
"""
Chronon3D Gel Text Template Renderer
Allows rendering production Gel Text with full optical passes (Streak, Drop Shadow,
Radial Zoom Blur, Spectral Caustic Aberration, Dual Atmosphere Glow) without
modifying or recompiling the core C++ engine.
"""

import argparse
import json
import os
import subprocess
import sys

CHRONON_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
CLI_PATH = os.environ.get("CHRONON_CLI") or os.path.join(
    CHRONON_ROOT, "build/chronon/linux-video-test/apps/chronon3d_cli/chronon3d_cli")
TEMPLATE_PATH = os.path.join(os.path.dirname(__file__), "compositions/GelText1080p.json")

def main():
    parser = argparse.ArgumentParser(description="Render Gel Text from template with full optical passes")
    parser.add_argument("--text", default="calories or ingredients", help="Text to render")
    parser.add_argument("--font-size", type=int, default=150, help="Font size in pt")
    parser.add_argument("--font", default="assets/fonts/Inter-Bold.ttf", help="Font path")
    parser.add_argument("--fill", default="#00E5FF", help="Base fill color")
    parser.add_argument("--glow-inner", type=float, default=6.0, help="Inner glow radius")
    parser.add_argument("--glow-outer", type=float, default=26.0, help="Outer atmospheric glow radius")
    parser.add_argument("--streak-length", type=float, default=64.0, help="Anamorphic 1D horizontal streak length")
    parser.add_argument("--zoom-amount", type=float, default=0.38, help="Radial zoom blur amount (0.0 - 1.0)")
    parser.add_argument("--duration", type=int, default=300, help="Duration in frames (30 fps, default 300 = 10s)")
    parser.add_argument("--output", default="output/gel_diag/09_Look_Finale_Streak_Aberrazione_1080p.mp4", help="Output mp4 path")
    parser.add_argument("--backend", default="vulkan", choices=["vulkan", "software"], help="Render backend")
    args = parser.parse_args()

    if not os.path.exists(CLI_PATH):
        print(f"Error: chronon3d_cli not found at {CLI_PATH}", file=sys.stderr)
        sys.exit(1)

    with open(TEMPLATE_PATH, "r") as f:
        plan = json.load(f)

    plan["canvas"]["duration_frames"] = args.duration
    plan["layers"][0]["duration_frames"] = args.duration

    # Streak Layer
    streak_layer = plan["layers"][1]
    streak_layer["text"] = args.text
    streak_layer["duration_frames"] = args.duration
    streak_layer["style"]["font_size"] = args.font_size
    streak_layer["style"]["font"] = args.font
    streak_layer["effects"][0]["length"] = args.streak_length

    # Core Gel Layer
    title_layer = plan["layers"][2]
    title_layer["text"] = args.text
    title_layer["duration_frames"] = args.duration
    title_layer["style"]["font_size"] = args.font_size
    title_layer["style"]["font"] = args.font
    title_layer["style"]["fill"] = args.fill
    title_layer["effects"][1]["radius"] = args.glow_inner
    title_layer["effects"][2]["radius"] = args.glow_outer
    title_layer["effects"][3]["amount"] = args.zoom_amount

    output_abs = os.path.abspath(args.output)
    plan["output"]["path"] = args.output
    os.makedirs(os.path.dirname(output_abs), exist_ok=True)

    job_plan_path = "/tmp/chronon_gel_job.plan.json"
    with open(job_plan_path, "w") as f:
        json.dump(plan, f, indent=2)

    cmd = [
        "nice", "-n", "15",
        CLI_PATH, "render",
        "--plan", job_plan_path,
        "--assets-root", CHRONON_ROOT,
        "--backend", args.backend
    ]
    print(f"Executing Chronon3D: {' '.join(cmd)}")
    subprocess.check_call(cmd, cwd=CHRONON_ROOT)
    print(f"Render complete: {args.output}")

if __name__ == "__main__":
    main()
