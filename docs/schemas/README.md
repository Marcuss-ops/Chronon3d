# Chronon3D JSON Schemas

This directory contains official JSON schemas for Chronon3D artifacts.

## `benchmark_report.schema.json`

Official schema for benchmark reports produced by the Chronon3D headless CPU renderer.

- **Schema identifier:** `chronon3d.bench.v3`
- **Scope:** Defines the canonical JSON format for performance benchmark reports, including timing percentiles, memory usage, quality/determinism metrics, and render counters.
- **Consumers:** `tools/compare_benchmarks.py`, telemetry dashboard, CI regression gates.

See [`chronon3d.bench.v3.schema.json`](chronon3d.bench.v3.schema.json) for the full schema definition.
