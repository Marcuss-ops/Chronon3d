"""Emit the minimal min-repro plan for the frozen repeater operator defect."""
import json
from pathlib import Path

j = Path("output/svg_frozen_repeater_repro")
(j / "shapes").mkdir(parents=True, exist_ok=True)

lottie = {
    "v": "5.7.4",
    "layers": [{
        "ty": 4, "ddd": 0,
        "ks": {"a": {"a": 0, "k": [0, 0]}, "p": {"a": 0, "k": [0, 0]},
               "s": {"a": 0, "k": [100, 100]}, "r": {"a": 0, "k": 0},
               "o": {"a": 0, "k": 100}},
        "shapes": [
            {"ty": "sh", "ks": {"a": 0, "k": {"c": True,
                "v": [[0, 0], [8, 0], [8, 8], [0, 8]],
                "i": [[0, 0], [0, 0], [0, 0], [0, 0]],
                "o": [[0, 0], [0, 0], [0, 0], [0, 0]]}}},
            {"ty": "fl", "r": 1, "c": {"a": 0, "k": [1, 0, 0, 1]}}],
    }],
}
(j / "shapes" / "repeater_source.json").write_text(json.dumps(lottie))

repeater_anim = {
    "easing": "linear",
    "keyframes": [
        {"frame": 0, "value": [0, 0, 1, 1, 0]},
        {"frame": 30, "value": [12, 0, 2, 1, 0]},
    ],
}
repeater_params = {"copies": 3, "position": [6, 0], "opacity_decay": 1.0,
                   "animation": repeater_anim}
operator = {"kind": "repeater", "params": repeater_params}
shape = {"type": "path", "lottie": "shapes/repeater_source.json",
         "operators": [operator]}
layer = {"id": "vector", "type": "shape", "size": [64, 64],
         "position": [32, 32], "shape": shape}
plan = {"schema": "chronon.render-plan.v3", "version": 3,
        "job_id": "repeater_frozen",
        "canvas": {"width": 64, "height": 64, "fps_num": 30, "fps_den": 1,
                   "duration_frames": 31},
        "layers": [layer],
        "output": {"path": "repeater.mp4", "format": "mp4"}}
(j / "plan.json").write_text(json.dumps(plan))
print("min-repro plan written to", j / "plan.json")
