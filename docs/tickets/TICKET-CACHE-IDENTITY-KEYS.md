# Cache identity and invalidation keys

- **Status:** ACTIVE — inventory and digest-preserving hashing split started
  2026-11-01; versioned semantic keys and mutation coverage remain open.
- **Owner:** Chronon3D cache/runtime maintainers.

## Goal

Make cache invalidation follow the semantic inputs that can change each cached
value. Keep the existing three cache families (`ContentCache`, `ResidencyCache`,
`ProgramCache`) and the canonical cache storage primitive. This work changes key
identity and dependency declarations; it does not add cache families or cache
instances.

The key axes to account for, where applicable, are:

| Axis | Inputs represented |
|---|---|
| Content | Source pixels and immutable source data: textures, shapes, fonts, mesh/material data |
| Placement | Projection, transform, bounds, masks, and affected compositing state |
| Time | Evaluated samples and animated properties, including sub-frame identity |
| Pipeline | Shader/program ABI, format, backend, and pass configuration |
| Output | Dimensions and final conversion/encoding parameters |

Each cache uses only the axes that affect its value. The axis names are a review
framework, not five new cache keys or caches.

## Work sequence

1. Inventory each cache's key fields and every key construction site; record
   which semantic inputs each field represents.
2. Split overloaded hash implementation by domain while preserving existing
   digest bytes and field order.
3. Introduce versioned semantic key construction one cache at a time. Replace
   raw object-byte hashing when object padding or non-canonical representation
   can enter the identity; keep scalar and explicitly canonical byte digests
   stable where required.
4. Add field-mutation tests for each key: changing every relevant field must
   change identity, while changing an unrelated field must not. Pin legacy
   digest fixtures before switching each key version.
5. Replace recursion-depth `0` digests with explicit validation/error handling
   or a validated input bound before key construction. A failed identity build
   must never masquerade as a valid cache key.

## Cache inventory

The ownership inventory is defined by `cache_taxonomy.hpp`; key-level progress
is recorded here as each cache is audited.

| Family | Cache / identity owner | Semantic dependencies |
|---|---|---|
| Content | NodeCache / `NodeCacheKey`, `NodeCacheIdentityBuilder` | Content, placement, time, pipeline and output where enabled by the three versioned hash slots |
| Content | FrameCache / `FrameCacheKey` | Scene content, sampled time, render/output configuration |
| Content | VideoFrameCache / `VideoFrameKey` | Video source content, sample and decode/output format |
| Content | ConvertedFrameCache / `ConvertedFrameCacheKey` | Frame content, destination format and conversion parameters |
| Content | GpuAssetCache / `GpuAssetKey` | Asset content digest, GPU format and extent |
| Content | PreparedAssetDigestCache | Resolved file identity and file metadata/digest |
| Content | GpuGlyphAtlas styled entries; GpuStyledGlyphCache | Font/glyph content, style, size and raster configuration |
| Content | GpuTextRunPreparedCache; DirectYuvTextTextureCache | Text/font content, preparation options, dimensions and output format; prepared-run identity includes the source TextLayoutCache V2 digest |
| Content | ImageCache / `ImageAssetKey` | Canonical path plus color, alpha, orientation, size-limit and OCIO decode options |
| Content | MeshPreparationCache; PreparationCache | Resolved mesh or font/glyph content and preparation options |
| Content | Software path flatten cache / `FlattenKey` | Path closure plus ordered command types/control points; the LRU retains and compares the complete geometry key after digest bucketing |
| Content | TextLayoutCache / `TextLayoutCacheKey` | Text, font, shaping, paragraph, wrap, geometry and layout options; V2 digest includes every `ParagraphStyle` field and LRU compares the full key |
| Content | TypewriterLayoutCache / `TypewriterLayoutKey` | Text, font, size, tracking, box and line height |
| Content | FrameValueCache | Delegates to NodeCache |
| Residency | FramebufferPool / `FramebufferPoolKey` | Width and height size class |
| Residency | GpuGlyphAtlas pages | Atlas page extent/format and residency slot |
| Residency | MediaSessionPool / `MediaSessionKey` | Codec, dimensions, pixel format, device and encoder/decoder role |
| Residency | BLFontFaceCache; FreeTypeFaceCache / `FTFaceKey` | Runtime font-face identity, file and face index |
| Program | TemplateProgramCache / `ProgramFingerprint` | Topology hash, renderer ABI and quality profile |
| Program | CompiledGraphCache / structure fingerprint | Graph topology, dimensions and compilation options |
| Program | RenderPipeline topology cache / `compiler::ProgramFingerprint` | Digest, algorithm version, content digest and request digest; topology LRU compares the complete fingerprint |
| Program | SceneProgramCache / `SceneStructureKey` | Topology, active set, render options and dimensions |
| Program | CurveCache / full `CurvePoint` vector | Curve samples and interpolation/configuration; V2 digest includes domain and count, while LRU equality disambiguates hash collisions |
| Program | CompiledArtifactCache / `ProgramArtifactKey` | Stable program name, target, compiler/backend and ABI attributes |
| Program | DeviceProgram / `DeviceProgramKey` | Pixel program, target, determinism class, specialization, compiler version, backend ABI and device capability |
| Program | CompiledTopologyCache | Delegates to CompiledGraphCache |

Initial observations: `NodeCacheKey` already has `params_version`,
`source_version`, and `input_version` in its digest. The canonical builder now
exposes per-axis versions; RenderNode content/placement, mask, video-source,
effect, and transition keys have begun adopting version 2. `FrameCacheKey` and
`VideoFrameKey` now expose their prior exact digest as `digest_v1()` and use
domain-tagged, fieldwise V2 digests for live hashing. VideoSource V2 also
hashes every field explicitly and canonicalizes signed zero in float fields;
its mutation test covers all fields and the signed-zero contract. Fill and
gradient V2 now hash scalar/vector/color components semantically, and fill V2
includes every `Field2DSampleGrid::values` sample (omitted by V1); mutation and
signed-zero tests pass while the V1 omission is pinned as compatibility.
The dependency map above assigns an identity owner to every cache in the
taxonomy. Mutation coverage is in place for the core frame/video/node keys,
conversion parameters, GPU asset key, program artifact key, graph V2 effect
fields, video source, clip transition, image decode options, GPU glyph fields,
converted-frame output settings, text-layout fields and compiler program
fingerprints. TextLayoutCache and CurveCache now retain complete semantic keys
in LRU entries instead of trusting a 64-bit digest as equality. Their older
digest contracts remain available where required; V2 digests are domain tagged.
TextRunLayout has a separate V2 prepared identity that folds in the full
TextLayoutCache key digest and list cardinalities; its pre-V2 digest remains
available for compatibility. The downstream GPU prepared-text cache test
checks that changing this upstream identity invalidates the prepared entry.
Text layout V1 is pinned at `15831138664930118102` in
`tests/text/test_layout_cache_collision.cpp`; curve V1 remains pinned by its
fixture in `tests/effects/test_curves.cpp`. Remaining cache-specific builders
still need dedicated mutation and legacy-fixture tests before their identities
can be called fully versioned. Structured mesh preparation identity, GPU glyph
key hashing, and FreeType face/bbox cache keys now have dedicated semantic
coverage. Runtime resource plans and compiled resource tables are placement
plans, not cache keys (the taxonomy names `CompiledResourceTable` as the
placement authority). Outstanding work includes deeper glyph-atlas page/raster
entry inventory and several opaque graph producer keys; legacy digest fixtures
are still missing for many newer V2 builders. The
software path flatten cache now
retains command vectors and compares each semantic field after digest bucketing;
its command/closure mutation test passes. `MediaSessionKey` has a field-by-field
equality mutation test. Renderer test binaries were built with `-j4`; the
focused path test and cache suite passed. A requested `-j12` build was stopped
after observing 18.7 GiB in twelve compiler processes (2.1 GiB available); the
resumed `-j4` build completed. The audit remains open for the outstanding
builders and legacy fixtures.

## Checkpoint 2026-11-01 — digest-preserving split

The former `src/render_graph/render_graph_hashing.cpp` implementation is split
into primitive/fill, effects, masks, shapes, and transition/RenderNode source
files. Function bodies and digest order were mechanically compared with the
pre-split source: identical (43,264 implementation characters). The
`chronon3d_graph_core` target compiled in `linux-video-fast-dev`. The
`linux-fast-dev` scene-test build was stopped after user direction to avoid high
parallelism. The later focused `-j2` build and eight-test CTest run are recorded
above. The initial key
audit now pins V1 digest fixtures and adds field-mutation checks for every field
in `FrameCacheKey`, `VideoFrameKey`, and `NodeCacheKey`. The test source passed
`-fsyntax-only`, and a standalone executable against the production digest
implementations passed all three golden digests and those field mutations.
These cache-key structs remain V1 pending their own semantic V2 contracts.

The graph hash API now keeps the original V1 functions and adds domain-tagged
V2 identities for RenderNode content/placement, effects, masks, shapes, fills,
video sources, and layer transitions. Production graph/cache callers have
migrated to V2. Effect parameter records previously hashed as whole objects are
hashed by member in V2, including every `Fake3DWaveParams` field; the transition
fallback is now an explicit `FlyUnderFadeParams` field hash. Clip transitions
now hash raw float values and full flash-color precision instead of
`std::hash<float>` and quantized color bytes. V2 mutation tests cover each
previously raw effect parameter field, video-source fields, and clip-transition
spec/sampling fields. Graph-core compilation passed with `-j2`; CTest and a
full test executable build have not yet completed.

The latest Shape V2 pass also replaces raw scalar/enum byte hashing with
fieldwise semantic hashing, canonicalizes signed zero in those fields, and
uses length-framed string hashing for image paths. Shape V1 retains its old
byte path. RoundedRect, Circle, Image and nested TextRun mutation tests cover
the new behavior; the remaining non-text Shape variants still need
field-complete mutation coverage.

Shape's TextRun variant now delegates to a separate TextRun V2 digest. It
includes complete resolved glyph state and vector lengths, all material and
paint fields (including fill/stroke styles and pattern pixels/transforms),
shadow stack, placement kind, and dissolve layout/glyph/mix state. Float fields
are canonicalized component-wise. The original TextRun digest remains the V1
path. Mutation tests cover formerly omitted glyph anchor, material,
stroke-dash cardinality, shadow and dissolve fields plus signed zero.

The transition-hashing audit found no recursion-depth-limited digest builder
or recursive `return 0` fallback in the render-graph hash implementation. The
zero returns found there are explicit legacy sentinels for disabled V1 masks
and the empty `monostate` transition alternative; V2 masks encode enabled
state and V2 transitions encode the variant index. This distinction is kept
for later compatibility review. LayerTransitionSpec V2 no longer wraps its
byte-oriented V1 digest: it hashes the domain, id, enum fields, duration,
delay, and active typed parameter members directly, canonicalizing signed
zero. The V1 builder remains unchanged; existing parameter mutation coverage
is supplemented with a V2 signed-zero test.

`GpuStyledGlyphCache::StyledGlyphKey` equality treats `+0.0` and `-0.0` as
equal, while its previous HashBuilder calls hashed their distinct float bit
patterns. The key hash now canonicalizes zero for every color and stroke-width
float so `unordered_map` hashing agrees with equality. A cache-level test
stores with positive zero and retrieves with negative zero.

`GpuGlyphKey` previously retained only the 64-bit hash of a font variation
axis string. The MTSDF atlas key now keeps the complete axis string for
equality and hashing, so distinct axis specifications cannot alias due to a
digest collision. The hash-only factory remains for source compatibility;
new text-run packing uses the full-string factory. Mutation coverage checks
full-axis equality, raster-size equivalence for MTSDF, and size sensitivity
for coverage glyphs.

`MeshPreparationCache` now stores `MeshIdentity` as its LRU key and uses a
fieldwise domain-tagged hasher. The LRU compares the full resolved path, byte
size, write time, and 32-byte content digest for equality; it no longer uses
the diagnostic `cache_key()` string as identity. Field mutation tests and a
cache hit/miss check cover the complete identity.

The renderer-owned `FreeTypeFaceCache` key now includes resolved path, face
index, quantized size, file byte size and last-write timestamp. This prevents
reusing an already-open face after an ordinary in-place file update. Tests
mutate every FTFaceKey field and verify a changed timestamp loads a distinct
face. `FontEngine::Impl::face_cache` now keys on the complete `FontSpec`, resolved
path, file byte size and last-write timestamp. Its loader opens the same resolved
path that was fingerprinted and rechecks the key after loading to avoid storing
a face if the file changed during the load. A same-path replacement test checks
that changed file metadata reloads new font metrics. This is file-stamp
invalidation rather than a content digest; edits that preserve both size and
timestamp can still reuse the cached face.

`GpuGlyphKey` now uses a domain-tagged V2 semantic digest in both its unordered
map hasher and diagnostic 64-bit digest. These identities are in-memory GPU
residency keys with no persisted artifact contract. Mutation coverage now checks
that each semantic field changes equality and both digest entry points; ignored
raster-size variation for distance-field glyphs remains covered separately.

Shape V2 mutation coverage now includes Rect, Line, GridBackground, FakeBox3D,
GridPlane and TiledImage payload fields, supplementing the existing Circle,
RoundedRect, Image and TextRun cases. This checks the fields the current V2
builder declares; a separate inventory comparison is still needed to prove the
builder includes every rendered field in each payload.

That comparison found Image and TiledImage V2 omitted fit mode, focal point,
crop, radius, decode options and embedded payload bytes. V2 now hashes these
fields (with payload length framing); V1 remains unchanged. Image mutation
coverage includes each added field and the Shape V2 focused suite passes.
Mesh V2 also hashes the prepared mesh identity's resolved path, byte size,
write time and content digest as individual fields instead of hashing its
diagnostic `cache_key()` string. Mutation coverage checks all four fields.

The FreeType glyph bounding-box key now has a domain-tagged V2 hasher over
face id, glyph id, pixel size and full variation-axis string. It uses a small
internal key header and has mutation coverage for each field.
`FramebufferPoolKey` also now uses a domain-tagged V2 fieldwise hasher over its
width and height size class; its residency identity remains entirely in memory.
`StyledGlyphKey` uses a domain-tagged V2 semantic hash, and a cache-level
mutation test verifies that every raster/presentation field misses the prior
entry.
The MTSDF prepared-glyph cache now uses a typed V2 key over full font content
digest, glyph id, variation axes and all four `MtsdfProfile` fields, with
field-mutation tests for each dependency. Its font-digest memo now also uses a
structured V2 stamp key with explicit metadata-validity bits instead of a
newline-concatenated string, and rechecks the stamp before memoizing a digest
read to avoid associating bytes with a changed file version.
Dynamic SourceNode, MultiSourceNode and TextRunNode NodeCache inputs now hash
matrix, vector and opacity components through padding-safe V2 primitives that
canonicalize signed zero. The matrix test mutates all 16 components.
MultiSource group opacity animation and ClipTransition V2 values now also use
canonical V2 float/color primitives instead of raw scalar/Color object bytes.
EffectStack V2 stays pinned to its legacy digest. A new EffectStack V3 routes
scalar, color and vector fields through semantic helpers; production node,
scene and dirty-region consumers now use V3. Glow and LUT domain bounds use
explicit V3 field hashing, and the complete parameter mutation suite checks
both V2 and V3 while signed-zero tests cover V3 float and color paths.
The cache test binary rebuilt at `-j4` and passed all 173 cases / 103,520
assertions after this producer-key change.

`TransformNode` previously folded `Transform` through the legacy helper, which
omits skew even though `Transform::to_mat4()` applies it. Its matrix and source
size were also hashed as object bytes. The node now uses a versioned semantic
transform digest (position, quaternion, scale, anchor, skew, opacity and blur),
component-wise matrix/source-size hashing, and `params_version = 2`. Mutation
tests cover every `Transform` component and signed-zero canonicalization; the
cache suite passes 175 cases / 103,545 assertions. The `chronon3d_core_tests`
target rebuilt at `-j4`; its TransformNode cache-key contract test passes.

The backend `TextRenderResources::GlyphAtlasKey` compared only `font_id`, while
path-based callers derived that ID from a 64-bit FNV hash. Path-based entries
now retain and compare the complete font path; the raw-ID overload occupies a
separate compatibility key namespace. Internal fused GPU text lookup now uses
the path overload. A collision simulation verifies distinct paths remain
distinct even when their old font IDs match, and a store/lookup test verifies
both entries round-trip independently. The focused test passes (7 assertions).
GPU atlas page reuse is partitioned by representation and full
`DistanceFieldProfile`; page residency keys also include format and extent,
with the page slot disambiguating profiles that share a representation.

## Exit criteria

- Every cache in the inventory has a key-field dependency map and owner.
- Every changed key version has pinned legacy fixtures and field-by-field
  mutation tests.
- Cache keys use semantic, padding-safe hashing for composite values.
- Invalid recursive/cyclic input cannot produce a valid-looking zero digest.
- The existing architecture and cache-family gates pass; no additional cache
  family, storage engine, or parallel registry is introduced.
