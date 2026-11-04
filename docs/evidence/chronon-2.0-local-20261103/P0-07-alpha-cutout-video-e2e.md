# P0-07 — alpha-cutout vs blending, video E2E (2026-11-03)

**Stato: PASS locale.** Il gate mancante era il video E2E: il contratto
alpha-test/cutout aveva copertura unit e pixel software, ma nessun clip che lo
dimostrasse. Ora c'è una fixture RenderPlan e un gate eseguibile che renderizza
24 frame sul lane raw pre-encode e verifica i pixel.

## Gate

```bash
cd Chronon3d
python3 tests/e2e/test_p0_07_alpha_cutout_video.py \
  --cli build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli
```

- Fixture: `tests/e2e/fixtures/p0-07-alpha-cutout-two-meshes.plan.json`
  (640×360, 24 frame, camera perspective con `focal = 1000`).
- Asset dei due piani generati dal gate stesso: nessuna dipendenza esterna.
- Sink: `--video-sink raw`, il lane pixel-truth canonico (P0-08).

## Cosa contiene la scena

| Layer | Geometria | Contratto |
|---|---|---|
| `backdrop` | piano `image` 3D a z = +300 | piano lontano, blu |
| `front-plane` | piano `image` 3D a z = −400, **dichiarato prima delle mesh** | deve vincere sulla mesh opaca per profondità, non per ordine |
| `opaque-red` | mesh `plane` opaca a z = 0 | copertura piena e stabile |
| `cutout-green` | mesh `plane`, `alpha_cutoff = 0.5`, opacità animata 0.2 → 0.8 | discard binario: zero pixel sotto soglia, footprint costante sopra |
| `fractional-cyan` | mesh `plane`, opacità 0.5, nessun cutoff | blending frazionario, non discard e non opaco |

## Assertion e misure

Il gate misura le regioni dai pixel stessi (bbox delle maschere) invece di
fissare coordinate a mano; il probe della mesh opaca è scelto massimizzando la
distanza dai bordi antialiasati del piano e della mesh frazionaria.

| Assertion | Misura |
|---|---|
| mesh opaca copre il proprio interno, ogni frame | ≥ 22 490 px |
| il piano anteriore vince sulla mesh, ogni frame | ≥ 17 954 px |
| cutout sotto soglia (frame 0..11, alpha 0.200..0.487) | **0 px** in ogni frame |
| cutout sopra soglia (frame 13..23, alpha 0.539..0.800) | **32 220 px**, identici in ogni frame |
| footprint cutout stabile sopra soglia | variazione 0% (binario, nessuna banda fantasma) |
| alpha frazionario vs riferimento opaco | b(0.5)=173, b(1.0)=230, backdrop=56 → rapporto 0.67 in sRGB (≈0.5 in lineare) |
| mesh frazionaria non scartata e non opaca | sì |
| box chiuso traslucido (regressione culling) | alpha 128/255, non 191/255 |

La seconda resa (mesh frazionaria a opacità 1.0) è la referenza per misurare il
rapporto di blend: senza di essa il test dovrebbe indovinare il colore atteso.

## Fix del volume chiuso (backface culling)

La prima misura su un `box` chiuso a opacità 0.5 dava alpha **191/255 (0.75)**:
il percorso software non faceva backface culling, quindi fronte e retro si
componevano. Non era una doppia applicazione dell'opacità (la faccia singola
`plane` dava già 128/255), ma un contratto mancante del rasterizer.

Fix: `mesh_renderer` culla le facce posteriori in tutti e tre i percorsi
(filled, textured, lit). Il lit rispetta `material.double_sided`; gli altri
accettano `BackfaceCulling::Enabled/Disabled` (enum forte, così un
`alpha_cutoff` float non può più bindare a un flag bool). Il test che usa un
triangolo deliberatamente rivolto all'indietro per verificare la diagonale del
fan passa `BackfaceCulling::Disabled` e conserva il proprio scopo.

Misure dopo il fix:

| Caso | Prima | Dopo |
|---|---|---|
| box chiuso a opacità 0.5, centro | 191/255 | **128/255** |
| piano a faccia singola a opacità 0.5 | 128/255 | 128/255 |
| `chronon3d_renderer_core_tests` | 204/206 | **206/206, 74 294 asserzioni** |

Il gate include ora la regressione: crea una variante con `box`, la renderizza
e verifica `118 <= alpha <= 138`. La parità Vulkan dello stesso culling non è
stata eseguita (lane GPU non disponibile, `BLOCKED`).

Limite residuo: `assets::MeshMaterial` (mesh glTF preparate) non espone
`double_sided`, quindi il culling resta attivo anche per un asset che dichiara
due facce. Per gli inline mesh il flag è rispettato; per i glTF serve portare
`double_sided` dal loader al materiale preparato (follow-up separato).

## Esito

`E2E_EXIT=0`, 24/24 frame, tutte le assertion verdi. I frame di prova sono in
`Chronon3d/reports/p0-07-alpha-cutout/` quando si esegue con `--keep`.
