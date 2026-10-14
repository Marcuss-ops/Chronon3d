#!/usr/bin/env python3
"""Fail when the two repositories' static inline-mesh RenderPlan contracts drift."""
from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MOTION_CONTRACT = "contracts/chronon.render-plan.inline-mesh.v1.json"
SCHEMA_PATHS = (
    "schemas/json/chronon.render-plan.v2.schema.json",
    "schemas/json/chronon.render-plan.v3.extensions.schema.json",
)


def fail(message: str) -> int:
    print(f"inline-mesh contract drift: FAIL: {message}", file=sys.stderr)
    return 1


def load_json(path: Path) -> dict:
    with path.open(encoding="utf-8") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise ValueError(f"expected an object: {path}")
    return value


def schema_mesh_block(schema: dict) -> tuple[dict, dict]:
    try:
        layers = schema["properties"]["layers"]["items"]
        inline_mesh = layers["properties"]["inline_mesh"]
        all_of = layers["allOf"]
    except (KeyError, TypeError) as error:
        raise ValueError(f"schema has no inline mesh layer block: {schema.get('$id', '<unknown>')}") from error

    mesh_condition = next((condition for condition in all_of
                           if condition.get("if", {}).get("properties", {})
                           .get("type", {}).get("const") == "mesh"), None)
    if mesh_condition is None:
        raise ValueError(f"mesh requirement is missing in {schema.get('$id', '<unknown schema>')}")
    consequent = mesh_condition.get("then", {})
    if "inline_mesh" not in consequent.get("required", []):
        raise ValueError(f"mesh layer does not require inline_mesh in {schema.get('$id', '<unknown schema>')}")
    forbidden = {"anyOf": [{"required": ["asset"]}, {"required": ["source"]}]}
    if consequent.get("not") != forbidden:
        raise ValueError(f"mesh layer asset/source prohibition differs in {schema.get('$id', '<unknown schema>')}")
    reverse = next((condition for condition in all_of
                    if "inline_mesh" in condition.get("if", {}).get("required", [])), None)
    if reverse is None or reverse.get("then", {}).get("properties", {}).get("type", {}).get("const") != "mesh":
        raise ValueError(f"inline_mesh is not exclusive to mesh layers in {schema.get('$id', '<unknown schema>')}")
    return inline_mesh, layers


def main() -> int:
    if len(sys.argv) != 2:
        return fail(f"usage: {Path(sys.argv[0]).name} <ChrononMotion3D checkout>")
    motion = Path(sys.argv[1]).resolve()
    if not (motion / "CMakeLists.txt").is_file():
        return fail(f"ChrononMotion checkout not found: {motion}")
    try:
        shared = load_json(motion / MOTION_CONTRACT)
        schemas = [load_json(ROOT / path) for path in SCHEMA_PATHS]
    except (OSError, json.JSONDecodeError, ValueError) as error:
        return fail(str(error))

    expected = shared.get("inline_mesh")
    if not isinstance(expected, dict):
        return fail("Motion contract is missing its inline_mesh schema")
    schema_blocks = []
    for schema in schemas:
        try:
            inline_mesh, layers = schema_mesh_block(schema)
        except ValueError as error:
            return fail(str(error))
        if schema.get("$id", "").endswith("v2.schema.json"):
            expected_static = {key: value for key, value in expected.items()
                               if key != "properties"}
            expected_static["properties"] = {key: value for key, value in
                                              expected["properties"].items()
                                              if key != "animation"}
            if inline_mesh != expected_static:
                return fail(f"static inline_mesh schema differs in {schema.get('$id', '<unknown schema>')}")
        else:
            actual_mesh = {key: value for key, value in inline_mesh.items()
                           if key not in {"primitive", "material", "oneOf"}}
            actual_mesh["properties"] = {key: value for key, value in
                                         inline_mesh["properties"].items()
                                         if key not in {"primitive", "material"}}
            expected_v3 = {key: value for key, value in expected.items()
                           if key != "required"}
            expected_v3["properties"] = {key: value for key, value in
                                          expected["properties"].items()
                                          if key not in {"primitive", "material"}}
            if actual_mesh != expected_v3:
                return fail(f"inline_mesh schema differs in {schema.get('$id', '<unknown schema>')}")
        schema_blocks.append((schema, layers))

    expected_contract = {
        "minimum_vertices": expected["properties"]["vertices"]["minItems"],
        "maximum_vertices": expected["properties"]["vertices"]["maxItems"],
        "minimum_indices": expected["properties"]["indices"]["minItems"],
        "maximum_indices": expected["properties"]["indices"]["maxItems"],
        "triangle_index_count_multiple": 3,
        "max_aggregate_payload_bytes": 64 * 1024 * 1024,
        "payload_vertex_bytes": 32,
        "payload_index_bytes": 4,
        "position_component_abs_max": 1_000_000,
        "normal_component_abs_max": 1,
        "uv_component_abs_max": 1_000_000,
        "maximum_animation_samples": 4096,
    }
    contract = shared.get("mesh_layer_contract", {})
    for key, value in expected_contract.items():
        if contract.get(key) != value:
            return fail(f"Motion contract {key} is {contract.get(key)!r}, expected {value!r}")
    if shared.get("contract_version") != 2 or shared.get("type_enum_value") != "mesh":
        return fail("shared contract version must be 2 for the V3 baked animation extension")
    v3_mesh_schema = schema_blocks[1][0]["properties"]["layers"]["items"]["properties"]["inline_mesh"]
    if expected.get("properties", {}).get("animation") != v3_mesh_schema["properties"].get("animation"):
        return fail("V3 baked animation schema differs from the shared Motion contract")
    v2_mesh_schema = schema_blocks[0][0]["properties"]["layers"]["items"]["properties"]["inline_mesh"]
    if "animation" in v2_mesh_schema.get("properties", {}):
        return fail("V2 inline_mesh must remain static; baked samples are V3-only")
    if contract.get("forbidden_fields") != ["asset", "source"]:
        return fail("mesh asset/source reference policy differs from the inline contract")
    if contract.get("inline_mesh_requires_type") != "mesh":
        return fail("inline_mesh reverse type requirement differs from the inline contract")

    for schema, layers in schema_blocks:
        if "mesh" not in layers.get("properties", {}).get("type", {}).get("enum", []):
            return fail(f"mesh type enum is missing in {schema.get('$id', '<unknown schema>')}")
    print("inline-mesh contract drift: PASS (complete Motion contract matches RenderPlan V2 and V3)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
