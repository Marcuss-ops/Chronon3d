#!/usr/bin/env python3
"""Contract smoke checks for JSON schema documents at the IPC boundary.

This intentionally does not implement JSON Schema validation. Runtime payload
validation belongs to the canonical validator boundary; this test only guards
schema document identity, sealed roots, and the public field surface.
"""

from pathlib import Path
import json


ROOT = Path(__file__).resolve().parents[1]
SCHEMA_DIR = ROOT / "schemas"

EXPECTED = {
    "chronon.composition.v1.schema.json": (
        "chronon.composition",
        {"schema", "version", "id"},
        {"id", "category", "width", "height", "fps", "duration"},
    ),
    "chronon.render-plan.v1.schema.json": (
        "chronon.render-plan",
        {"schema", "version", "canvas", "layers", "output"},
        {"job_id", "style_profile", "canvas", "layers", "budget", "output"},
    ),
    "chronon.render-settings.v1.schema.json": (
        "chronon.render-settings",
        {"schema", "version"},
        {
            "width",
            "height",
            "antialiasing_samples",
            "motion_blur",
            "dirty_rects",
            "deterministic",
        },
    ),
}


def main() -> None:
    for filename, (schema_name, required, fields) in EXPECTED.items():
        path = SCHEMA_DIR / filename
        document = json.loads(path.read_text(encoding="utf-8"))

        assert document["$schema"] == "https://json-schema.org/draft/2020-12/schema"
        assert document["$id"].endswith(filename)
        assert document["type"] == "object"
        assert document["additionalProperties"] is False
        assert document["properties"]["schema"]["const"] == schema_name
        assert document["properties"]["version"]["const"] == 1
        assert set(document["required"]) == required
        assert fields.issubset(document["properties"])

    settings = json.loads(
        (SCHEMA_DIR / "chronon.render-settings.v1.schema.json").read_text(
            encoding="utf-8"
        )
    )
    assert "gpu_device" not in settings["properties"]
    assert "encoder" not in settings["properties"]
    assert "backend" not in settings["properties"]

    print("IPC schema document checks: PASS (3 v1 schemas)")


if __name__ == "__main__":
    main()
