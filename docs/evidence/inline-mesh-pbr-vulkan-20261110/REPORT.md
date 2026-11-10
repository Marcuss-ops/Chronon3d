# E1 — Vulkan inline-mesh metallic-roughness, gate end-to-end (2026-11-10)

**Esito: PASS locale.** Il piano V3 con quattro mesh a **stessa base color** e
**MR diversa** produce pixel diversi su entrambe le lane, e la parità
software↔Vulkan sull'interno delle mesh è esatta. Albero di lavoro dirty: questa
è evidenza locale, **non** certificazione same-SHA.

## Difetto trovato e corretto

`decode_layer_inline_mesh()` leggeva `inline_mesh.metallic_roughness_texture`
**prima** di espandere `inline_mesh.primitive`. L'assegnazione
`mesh = expand_inline_mesh_primitive(primitive)` sostituiva l'intero
`InlineMeshPlan` e cancellava il percorso della texture. Risultato: sui piani
con `primitive` la MR veniva ignorata in silenzio da **entrambe** le lane,
mentre la lane Vulkan per-pixel risultava già implementata. Il parsing è stato
spostato dopo la costruzione della geometria
(`src/render_plan/render_plan_decoder_layer_geometry.cpp`).

## Fixture

`pbr-two-materials.plan.json`, 640×360, 1 frame, camera z=-1000, fov 20.4074.
Quattro layer `mesh` con `primitive: plane`, stessa `base_color`
`[0.7, 0.7, 0.7, 1]`, fattori `metallic=1`, `roughness=1` e texture ORM 4×4:

| Layer | G (roughness) | B (metallic) | Scopo |
|---|---:|---:|---|
| `rough-dielectric` | 1.00 | 0.00 | G alto |
| `smooth-dielectric` | 0.00 | 0.00 | G basso |
| `metal` | 0.20 | 1.00 | B alto |
| `mid-dielectric` | 0.20 | 0.00 | B basso |

La key directional è obliqua (`[1.0, 0.0, 0.6]`, ambient 0.3) così il termine
speculare dipende davvero da `shininess` e il diffuse da `1-metallic`. Le
texture sono generate in `mr_*.png` e spedite con la fixture.

## Comandi

```sh
cmake --build build/chronon/linux-video-release \
  --target chronon3d_cli chronon3d_c_abi_tests -j12

python3 tools/verify_inline_mesh_pbr_parity.py \
  --plan docs/evidence/inline-mesh-pbr-vulkan-20261110/pbr-two-materials.plan.json \
  --assets-root docs/evidence/inline-mesh-pbr-vulkan-20261110 \
  --out docs/evidence/inline-mesh-pbr-vulkan-20261110/run
```

## Risultati

| Controllo | Esito |
|---|---|
| `chronon3d_c_abi_tests --test-case="*inline*"` | 10/10 casi, 132/132 assert |
| `chronon3d_backend_registry_tests --test-case="Vulkan inline mesh samples metallic-roughness per pixel"` | 1/1 caso, 16/16 assert |
| `chronon3d_compiler_boundary_tests` | 14/14 casi, 192/192 assert |
| `tools/check_architecture.py` | 218/218 regole |
| Errori di validazione Vulkan nel log | 0 |
| `alpha_iou` software/Vulkan | 0.99375 (soglia ≥ 0.99) |
| MAE RGB interno mesh | 0.0 (soglia ≤ 2/255) |
| p99 RGB interno mesh | 0.0 (soglia ≤ 8/255) |
| Delta luma G, software / Vulkan | 0.01176 / 0.01176 |
| Delta luma B, software / Vulkan | 0.23529 / 0.23529 |

Soglie preregistrate in `tools/verify_inline_mesh_pbr_parity.py` prima del primo
run; nessuna è stata abbassata. La ROI è la maschera alpha erosa di 3 px, non il
full-frame.

## Artefatti

- `pbr-two-materials.plan.json` — SHA-256 `b8ab8d9424161b279fc47283a1063aa8fc81529c45a1931604c4c3358fd1fb39`
- `run/software.raw` — SHA-256 `0d824f9dd5bbe302f5d30d9ba54b64605675dcff936b2e9c4e52d6ff2957df3a`
- `run/vulkan.raw` — SHA-256 `e4d7df340cf597dc0f213244b1cdd6cb55d9ca6a961c7b1650c9f9eee4daa3b4`
- CLI — SHA-256 `69a5e6e8f94fd326d7dfef3320bac296216a64327a8dd5461b1c02ac8cf7a8c9`
- `run/report.json`, `run/software.log`, `run/vulkan.log`, `run/software.png`, `run/vulkan.png`

## Limiti

- Un solo frame e quattro quad piatti: prova il consumo della MR, non la
  conformità PBR su glTF animati, normal map, occlusion o clearcoat.
- La fixture usa `lighting_rig`; un layer `type: light` su questa lane fallisce
  con `lighting layer '…' requires a valid native surface or CPU pixel backing`.
  È un limite separato, non mascherato da questo gate.
- Albero dirty: la certificazione same-SHA resta aperta (program ticket E10).
