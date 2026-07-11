# Chronon3D CLI Reference

This document describes the `chronon3d_cli` subcommands. For installation
and SDK usage, see [`README.md`](../README.md) and
[`docs/CURRENT_STATUS.md`](CURRENT_STATUS.md).

## Synopsis

```bash
chronon3d_cli <subcommand> [options]
```

## Subcommands

| Subcommand | Description | Group |
|---|---|---|
| `list` | List all registered compositions | Core (always available) |
| `info <id>` | Show composition metadata | Core |
| `doctor` | Check whether the local environment is ready | Core |
| `verify` | Run a quick render and video smoke test | Core |
| `render <id>` | Render a composition to PNG(s) | Render |
| `video <id>` | Render a composition to MP4 via ffmpeg | Video |
| `still <id>` | Render a single frame with asset preflight | Render |
| `graph <id>` | Inspect the render graph (summary or DOT export) | Dev (inspect) |
| `camera-path <id>` | Export camera path as JSON/CSV | Dev (inspect) |
| `preflight <id>` | Validate assets and requirements before rendering | Dev (inspect) |
| `inspect-text <id>` | Per-node TextRun audit with structured JSON output | Dev (inspect) |
| `text-audit <id>` | Multi-frame text layout audit (legacy) | Dev |

---

## `inspect-text` — Per-node TextRun audit (§12 FU09)

> **Status:** §12 atomic commit (2026-07-10, M1.8 §4B / TICKET-SIMPLICITY-INSPECT-TEXT).
> **Gate:** Requires `CHRONON3D_BUILD_DIAGNOSTICS=ON` (in non-diagnostic builds the
> command emits an error JSON and exits 1).

### Synopsis

```bash
chronon3d_cli inspect-text <composition_id> --frame N [--json]
```

### Description

Renders the composition at frame `N` and emits a structured JSON array
describing each TextRun node's audit state. The audit evaluates the 4
critical invariants from the FU02 `TextVisibilityAudit` contract:

- `font_resolved` — `shape.engine != nullptr`
- `shaping_succeeded` — `shape.layout.placed.glyphs.size() > 0`
- `finite` — all bboxes have finite coordinates
- `predicted_contains_world` — `predicted_bbox ⊇ world_ink_bbox` (FU04)

The per-node `status` field is one of:

- `"PASS"` — all critical invariants hold
- `"FAIL"` — at least one critical invariant is false
- `"VIOLATION"` — `predicted_contains_world` is false (FU04 trigger; the
  `TextRunNode::predicted_bbox()` substitutes the `expanded_predicted_bbox`
  in this case)

### Options

| Option | Required | Default | Description |
|---|---|---|---|
| `id` (positional) | yes | — | Composition name |
| `--frame N` | yes | — | Frame number to inspect |
| `--json / --no-json` | no | `true` | Emit JSON to stdout (default: on) |

### Output format

The output is a JSON array (one entry per TextRun node). The minimal
implementation emits a single entry per composition; a future FU10
follow-up will emit one entry per node for multi-node compositions.

```json
[
  {
    "node": "MyComp_text",
    "font": "<font>",
    "glyph_count": 5,
    "frame": 0,
    "local_bbox":    { "x0": 0.0, "y0": 0.0, "x1": 1.0, "y1": 1.0 },
    "world_bbox":    { "x0": 0.0, "y0": 0.0, "x1": 1.0, "y1": 1.0 },
    "predicted_bbox": { "x0": 0.0, "y0": 0.0, "x1": 1920.0, "y1": 1080.0 },
    "alpha_bbox":    { "x0": 0.0, "y0": 0.0, "x1": 0.0, "y1": 0.0 },
    "status": "PASS"
  }
]
```

### Exit codes

| Code | Meaning | Trigger |
|---|---|---|
| 0 | PASS | All nodes pass critical invariants |
| 1 | FAIL | Composition not found, non-diagnostic build, or any node has `font_resolved=false` / `finite=false` / `shaping_succeeded=false` |
| 2 | VIOLATION | Any node has `predicted_contains_world=false` (FU04 trigger) |

### Examples

#### PASS case (exit 0)

```bash
$ chronon3d_cli inspect-text MyComp --frame 0
[
  {
    "node": "MyComp_text",
    "font": "<font>",
    "glyph_count": 5,
    "frame": 0,
    "local_bbox":    { "x0": 0.0, "y0": 0.0, "x1": 1.0, "y1": 1.0 },
    "world_bbox":    { "x0": 0.0, "y0": 0.0, "x1": 1.0, "y1": 1.0 },
    "predicted_bbox": { "x0": 0.0, "y0": 0.0, "x1": 1920.0, "y1": 1080.0 },
    "alpha_bbox":    { "x0": 0.0, "y0": 0.0, "x1": 0.0, "y1": 0.0 },
    "status": "PASS"
  }
]
$ echo $?
0
```

#### FAIL case (composition not found, exit 1)

```bash
$ chronon3d_cli inspect-text NonExistent --frame 0
{
  "error": "composition_not_found",
  "composition_id": "NonExistent",
  "status": "FAIL"
}
$ echo $?
1
```

#### Non-diagnostic build (exit 1)

```bash
$ chronon3d_cli inspect-text MyComp --frame 0
{
  "error": "diagnostics_disabled",
  "status": "FAIL",
  "message": "inspect-text requires CHRONON3D_BUILD_DIAGNOSTICS=ON; rebuild with diagnostics enabled to use this subcommand"
}
$ echo $?
1
```

### See also

- [`text-audit`](CURRENT_STATUS.md) — multi-frame text layout audit
  (legacy, batch-mode with JSON file output)
- [ADR-019 §6 — Text Coordinate Model Numerical Examples](adr/ADR-019-text-coordinate-model.md#6-numerical-examples-19201080-canonical-canvas)
  — coordinate-level invariants
- [`docs/CHANGELOG.md` §12 entry](CHANGELOG.md) — atomic commit notes
- [`docs/ROADMAP.md` M1.8 §4B row](ROADMAP.md) — M1.8 milestone status
- [`docs/CHANGELOG.md` §9 entry](CHANGELOG.md) — `TextVisibilityAudit`
  contract (FU02/FU04) underpinning this subcommand

---

## `inspect-text` internals (forward-point)

The §12 implementation is the minimal version: it renders the composition
at the requested frame, calls `audit_text_visibility()` once (with the
tight predicted bbox from the composition's canvas), and emits a single
JSON entry. The full FU10 follow-up will:

1. Walk the render graph to enumerate all TextRunNodes
2. Emit one JSON entry per node
3. Compute the per-node `world_matrix` from the TextRunPlacement
4. Extract the per-node `font` field from `shape.engine->font_path()`
5. Support `--frame-range START-END` for multi-frame inspection
