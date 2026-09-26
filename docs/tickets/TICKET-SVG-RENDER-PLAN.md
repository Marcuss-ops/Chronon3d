# TICKET-SVG-RENDER-PLAN — Resolve SVG assets into shape plans

## Stato
CONTRACT APPROVED — implementation begins after this contract commit.

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

- [ ] Resolver-backed SVG plan decode lowers supported paths and primitives;
  missing, escaped, malformed and `<text>` assets fail closed.
- [ ] Golden geometry fixtures cover path commands, circles/arcs, transforms,
  document order and 1e-4 quantization.
- [ ] Resource-limit fixtures cover document bytes, 100k XML nodes, command
  count and out-of-bound coordinates.
- [ ] Fingerprint is stable for normalized imported geometry and changes when
  geometry changes.
- [ ] End-to-end render-plan SVG produces a non-empty deterministic frame hash.
- [ ] Focused asset/render-plan tests and relevant renderer suite pass.

## Demolition debt

- **Owner:** assets and render-plan maintainers.
- **Reason:** the SVG importer exists but no render-plan or resolver boundary
  consumes it.
- **Exit condition:** all acceptance gates pass on one source SHA.
- **Removal scope:** none; this ticket establishes the first resolver-backed
  SVG path and its permanent limits.
