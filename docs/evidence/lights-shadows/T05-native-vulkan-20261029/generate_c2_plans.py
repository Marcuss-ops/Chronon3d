#!/usr/bin/env python3
"""Write deterministic 1080p, one-frame particle workloads for T05/C2."""

import json
from pathlib import Path


HERE = Path(__file__).resolve().parent


def main() -> None:
    for count in (1_000, 10_000, 100_000):
        plan = {
            "schema": "chronon.render-plan.v3",
            "version": 3,
            "job_id": f"t05-c2-particles-{count}",
            "canvas": {
                "width": 1920,
                "height": 1080,
                "fps_num": 30,
                "fps_den": 1,
                "duration_frames": 1,
            },
            "output": {
                "path": f"c2-{count}.mp4",
                "format": "mp4",
                "codec": "h264",
            },
            "layers": [
                {
                    "id": "background",
                    "type": "color",
                    "size": [1920, 1080],
                    "color": [0.025, 0.035, 0.06, 1.0],
                    "duration_frames": 1,
                },
                {
                    "id": "particles",
                    "type": "particles",
                    "position": [960.0, 540.0],
                    "duration_frames": 1,
                    "particle_emitter": {
                        "count": count,
                        "seed": 20261029,
                        "emitter_size": [1600.0, 850.0],
                        "velocity_min": [-12.0, -12.0],
                        "velocity_max": [12.0, 12.0],
                        "lifetime": [2.0, 6.0],
                        "size": [1.0, 5.0],
                    },
                },
            ],
        }
        path = HERE / f"c2-{count}.plan.json"
        path.write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8")
        print(path.name)


if __name__ == "__main__":
    main()
