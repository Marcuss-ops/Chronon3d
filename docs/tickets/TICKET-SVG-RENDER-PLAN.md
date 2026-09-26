# TICKET-SVG-RENDER-PLAN — Resolve SVG assets into shape plans

## Stato
IN PROGRESS — resolver-backed lowering, deterministic normalization, and limits are implemented. Resolver-backed CLI render and framehash now pass; the remaining gap is that differently styled SVG subpaths are rejected rather than expanded into multiple shape layers.

## Obiettivo
Allow a render plan shape to refer to an SVG asset through the mounted
`AssetResolver`. Import its geometry into the existing `ShapePathCommandPlan`
vocabulary before graph compilation; rendering continues through the current
shape pipeline.

## Contratto

- Only `.svg` assets are imported. The logical path is resolved with
  `AssetResolver::resolve_logical`; absolute paths, traversal, missing assets,
  malformed XML and unsupported SVG elements fail with typed plan diagnostics.
- SVG `<text>` is explicitly unsupported and must fail; no font lookup or
  implicit text conversion occurs.
- Supported SVG path and primitive geometry is lowered to the current shape
  path commands in document order. Styles are mapped only where the existing
  shape model can represent them; unsupported style semantics fail explicitly.
- Input and expansion are bounded: maximum document bytes, XML nodes, emitted
  path commands and absolute coordinate magnitude are fixed constants. Exceeding
  a bound returns a typed resource-limit error before graph construction.
- All emitted coordinates are finite and quantized to 1e-4 before conversion
  to the plan's float representation and before fingerprinting. Equivalent
  imported geometry therefore hashes from the normalized lowered commands.
- Existing inline path plans remain backward compatible. No rasterizer,
  renderer, or second asset loading path is added.

## Acceptance gates

- [x] Resolver-backed SVG plan decode lowers same-style paths and primitives;
  missing/escaped references fail closed. Malformed and `<text>` loader tests pass.
- [x] Geometry fixtures cover path commands, arcs, transforms, document order,
  and 1e-4 quantization.
- [x] Resource-limit fixtures cover 8 MiB input, 100k XML nodes, 65536 path
  commands, and out-of-bound coordinates.
- [x] The SVG source digest enters the prepared manifest; normalized lowered
  commands enter the content fingerprint. Unit coverage verifies plan fingerprint behavior.
- [x] End-to-end CLI plan render produces a visible 64x64 PNG; decoding it to
  tightly packed RGBA and running `tools/raw_frame_hash.py` produced frame hash
  `4fe7b59af6de3b665b67788cc2f99892ab827efae3a467342b3bb4e3bc8e5bfe` twice.
- [x] Focused SVG importer and C ABI/render-plan tests pass. Relevant renderer
  suite passed before enabling SVG; rerun after remaining integration gates.

## Demolition debt

- **Owner:** assets and render-plan maintainers.
- **Reason:** the SVG importer exists but no render-plan or resolver boundary
  consumes it.
- **Exit condition:** all acceptance gates pass on one source SHA.
- **Removal scope:** none; this ticket establishes the first resolver-backed
  SVG path and its permanent limits.
