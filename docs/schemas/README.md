# Chronon3D JSON Schemas

This directory contains official JSON schemas for Chronon3D artifacts.

## `benchmark_report.schema.json`

Official schema for benchmark reports produced by the Chronon3D headless CPU renderer.

- **Schema identifier:** `chronon3d.bench.v3`
- **Scope:** Defines the canonical JSON format for performance benchmark reports, including timing percentiles, memory usage, quality/determinism metrics, and render counters.
- **Consumers:** `tools/compare_benchmarks.py`, telemetry dashboard, CI regression gates.

See [`chronon3d.bench.v3.schema.json`](chronon3d.bench.v3.schema.json) for the full schema definition.

## Daemon IPC boundary — V1 contracts

The daemon accepts JSON documents only at explicit boundary fields; the
FlatBuffers envelope remains the wire format. These three Draft 2020-12
schemas are the canonical structural contracts:

| Schema | Boundary payload | Required root fields |
|---|---|---|
| [`chronon.composition.v1.schema.json`](chronon.composition.v1.schema.json) | `CreateCompositionRequest.descriptor_json` | `schema`, `version`, `id` |
| [`chronon.render-plan.v1.schema.json`](chronon.render-plan.v1.schema.json) | render-plan compile input | `schema`, `version`, `canvas`, `layers`, `output` |
| [`chronon.render-settings.v1.schema.json`](chronon.render-settings.v1.schema.json) | neutral SDK/daemon settings | `schema`, `version` |

All three roots are sealed with `additionalProperties: false`. Structural
checks cover types, ranges, enums and nested object shape. The daemon still
performs semantic checks after decoding: composition registry lookup,
`composition_id`/descriptor identity, asset resolution, preset support,
resource budgets and backend capabilities. Backend-specific renderer and
encoder options are intentionally not part of the render-settings v1 surface.

The IPC descriptor reference is documented in
[`schema/chronon_ipc.fbs`](../../schema/chronon_ipc.fbs), while the schema
smoke test is `chronon3d_ipc_schema_documents_tests`.
