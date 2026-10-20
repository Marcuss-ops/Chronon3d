# TICKET-VULKAN-STROKE-ONLY-DRAW — Vulkan drops stroke-only path shapes

## Stato
OPEN — riproduzione minima archiviata in questo checkout (2026-10-19).

## Sintomo
Un layer `type: "shape"` con `shape.type: "path"`, `stroke` e senza `fill` non
produce alcun pixel sul backend `vulkan` del binario video-release
(`git_sha 9481092c8`, il CLI precedentemente usato per il word-rack): il canvas
resta solo il layer di sfondo. Lo stesso piano con `--backend software`
disegna il tratto correttamente.

## Riproduzione (archiviata)

- Piano minimo: `output/example_svg/probe_vulkan_stroke_only.plan.json`
  (bg `#060606` + layer `ring_outer`: path circolare a 96 segmenti, raggio 150
  centrato in (0,0), stroke `#7D7D64` width 9 cap/join round, niente fill,
  niente operators, niente effects).
- Comandi (cwd la root del workspace):

```bash
CLI=Chronon3d/build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli
$CLI render --plan output/example_svg/probe_vulkan_stroke_only.plan.json \
    --assets-root "$PWD" --backend software -o /tmp/sw.mp4   # disegna
$CLI render --plan output/example_svg/probe_vulkan_stroke_only.plan.json \
    --assets-root "$PWD" --backend vulkan -o /tmp/vk.mp4     # non disegna
```

- Artefatti archiviati: `probe_stroke_only_software.mp4` (ring visibile,
  bright% 0.852, bbox (0,0)-(149,149)) e `probe_stroke_only_vulkan.mp4`
  (bright% 0.0, nessun pixel sopra la soglia di luma 60).
- Exit code 0 in entrambi i casi: il guasto è silenzioso (fail-open), non
  segnalato da decoder né validatore.

## Contorno

- Per il delivery del 2026-10-19 (`example_svg_recreation.mp4`) si è usato
  `--backend software`, che disegna correttamente stroke-only.
- Aggiungere un `fill` potrebbe cambiare il ramo di paint e aggirare il
  difetto; non verificato in questa riproduzione.

## Hypotesis / prossimi passi

1. Ridurre a un caso nel preset `linux-video-test` (CTest) prima di toccare il
   render code: piano V3 inline → decode → compile → `TestRenderer` su surface
   vulkan, con assert su coverage alpha > 0. Un assert di coverage rende il
   difetto fail-closed nella suite esistente.
2. Ispezionare il draw del path nella pipeline Vulkan: probabile ramo di paint
   che richiede un payload `fill`, o lo stroke che salta la geometria outlined
   quando non c'è fill.
3. Aggiungere un gate di parità encoded software-vs-vulkan sul piano di
   riproduzione (pixel-check a soglia) per bloccare regressioni future.

## Riferimenti
- `PROBLEMS.md` §Aperti (2026-10-19) — voce 2 rimanda a questo ticket.
- Scena che ha esposto il difetto:
  `ChrononTemplate/tools/showcases/emit_example_svg_recreation_plans.cpp`
  (ricreazione Example Svg del 2026-10-19).
