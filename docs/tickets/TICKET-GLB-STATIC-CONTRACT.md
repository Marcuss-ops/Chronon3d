# TICKET-GLB-STATIC-CONTRACT — Bounded MeshLoader V1

## Stato

IMPLEMENTED — input and decoded-expansion bounds are enforced before scene
traversal or geometry allocation. Embedded material images are decoded under
an aggregate decoded-size limit for software PBR sampling.

## Contract

MeshLoader V1 accepts only self-contained GLB with one JSON chunk followed by
one BIN chunk and exactly one buffer backed by it; chunks must be 4-byte
aligned, and additional chunk types are rejected. It accepts triangle
primitives with float `POSITION`, optional matching
float `NORMAL` / `TEXCOORD_0`, indices, embedded images, base-color materials,
and static node transforms. Node graph order and transforms are baked during
preparation.

| Resource | V1 maximum |
| --- | ---: |
| Complete GLB file | 64 MiB |
| JSON chunk | 16 MiB |
| Scenes | 4,096 |
| Nodes / node visits | 100,000 |
| Node hierarchy depth | 256 |
| Active mesh instances | 4,096 |
| Mesh primitives / prepared parts | 8,192 |
| Aggregate expanded vertices | 1,000,000 |
| Aggregate expanded indices | 3,000,000 |
| Materials | 4,096 |
| Textures | 256 |
| Embedded images | 256 |
| Sum of copied embedded image bytes | 64 MiB |

Embedded images must use an in-file `bufferView` and explicitly declare
`image/png` or `image/jpeg`. External image URIs, image/texture extensions,
texture samplers, and other MIME types are rejected. The imported material
subset retains `baseColorFactor`, `baseColorTexture`, `metallicFactor`,
`roughnessFactor`, and `metallicRoughnessTexture` using `TEXCOORD_0`.
Metallic-roughness images are retained as encoded bytes and decoded to bounded
RGBA8 storage for software sampling. Alpha, normal, occlusion, and
emissive material inputs remain outside V1.

The aggregate geometry limits count repeated mesh instances after transform
baking, so a small source mesh instanced many times cannot bypass output-memory
bounds. Nodes referenced more than once are rejected because V1 models a single
tree, not a shared-node DAG.

glTF JSON with external buffers, multiple JSON/BIN chunks, non-triangle
primitive modes, animation clips, skins, morph targets, and unsupported
attributes/material extensions fail explicitly. Sparse accessors, cameras,
samplers, unsupported PBR fields, and non-finite transforms/geometry are
also rejected instead of being silently omitted. Group opacity/material
features not represented by `MeshMaterial` are outside this contract. It does
not claim full glTF 2.0 support.

## Acceptance

- [x] Define public constants for each bounded input/expansion resource.
- [x] Enforce file and JSON size before full parsing; enforce decoded counts
  before reserving output or reading each primitive's components.
- [x] Bound recursive traversal depth and reject cyclic/shared node references.
- [x] Add source fixtures for file size, JSON size, node count, and expanded
  vertex limits.
- [x] Reject duplicate, reordered, unknown, misaligned, and trailing GLB
  chunks at the container boundary.
- [x] Reject declared glTF extensions, unsupported primitive attributes,
  sparse accessors, and PBR fields that the prepared mesh model cannot retain;
  author explicit contract fixtures.
- [x] Bound embedded image payload copies and explicitly limit images to PNG/JPEG
  and texture sampling to the default sampler contract.
- [x] Run focused MeshLoader/resource-preparation tests and verify accepted
  fixtures retain prepared geometry and identity behavior.

## Verification

Mesh-enabled resource-preparation tests passed: embedded material loader
fixture (26 assertions) and bounds/range fixture (20 assertions). The
mesh-enabled build tree used for these checks had stale HarfBuzz paths; the
separate `linux-fast-dev` tree built and ran the scene-builder card API test.
