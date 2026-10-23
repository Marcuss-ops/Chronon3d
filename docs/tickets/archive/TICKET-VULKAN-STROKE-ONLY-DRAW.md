# TICKET-VULKAN-STROKE-ONLY-DRAW — Vulkan drops stroke-only path shapes

## Stato
RESOLVED (2026-10-20) — causa individuata, fix in `43db75c22`, regressione a
livello di routing in `tests/backends/test_vulkan_debug_context.cpp`. La
riproduzione minima resta archiviata come prova storica.

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

## Causa e fix (verificati 2026-10-20)

1. **Causa.** In `src/backends/vulkan/vulkan_native_shape_dispatch.cpp`,
   `try_native_path_stroke` accettava solo `LineCap::Butt`:

   ```cpp
   if (!path.stroke.enabled || path.stroke.width <= 0.0f ||
       path.stroke.gradient.has_value() || path.stroke.cap != LineCap::Butt ||
       graph::native_promotion::has_active_mask(state))
       return false;
   ```

   Il piano di riproduzione usa `cap: round`, quindi la rotta nativa veniva
   rifiutata. `try_native_path_fill` rifiuta a sua volta (nessun fill) e
   `try_native_solid_rect` non copre `ShapeType::Path`: senza fallback
   agganciato `draw_node` restituisce `UnsupportedCapability`, che la CLI
   ignora uscendo 0 → video col solo sfondo. Il difetto era nel **routing**,
   non nel kernel di stroke (che già disegnava).
2. **Fix.** Commit `43db75c22` accetta `Butt` e `Round` e inoltra il flag
   `path.stroke.cap == LineCap::Round` a `fill_dashed_path_surface`.
3. **Prova runtime.** Sul CLI Release corrente lo stesso piano rende
   software vs vulkan byte-identico: diff massima 0.0, 2553 pixel sopra
   luma 10, bbox (0,0)-(154,154); `CHRONON3D_PATH_DIAG=1` mostra
   `[native-path-dispatch-diag] ... runs=1 points=98` e nessun
   `native path routes rejected`. L'artefatto pre-fix
   `probe_stroke_only_vulkan.mp4` resta a 0 pixel sopra soglia.
4. **Regressione.** `tests/backends/test_vulkan_debug_context.cpp`,
   caso `Vulkan stroke-only path with round caps draws through draw_node`:
   costruisce un ring stroke-only (`cap: round`, 32 segmenti, 64×64),
   confronta pixel-per-pixel con l'oracle software
   (`renderer::PathRasterizer`) e chiude il buco fail-open con
   `REQUIRE(draw.ok())` + copertura alpha > 0. Con la condizione pre-fix
   ripristinata il caso fallisce su `REQUIRE(draw.ok())`; con il fix
   l'intero binario passa 11/11 casi, 34876 asserzioni.

Perché il test precedente non lo vedeva: `tests/registry/test_backend_registry_vulkan_dispatch.cpp`
chiama `fill_dashed_path_surface` direttamente e quindi non esercita il
routing `draw_node → try_native_path_fill/try_native_path_stroke`.

## Gate di parità (implementato 2026-10-20)

`scripts/ci/check_vulkan_stroke_only_parity_gate.py` + fixture
`golden/vulkan_stroke_only_gate/plan.json` (canvas 320×240, ring stroke-only
`cap: round`, nessuno sfondo: così la copertura misura **solo** il tratto).

- Rende la fixture col sink raw pre-encode (`--video-sink raw`), che è la
  superficie pixel-truth del repository, su `--backend software` e
  `--backend vulkan`, e confronta i frame **byte per byte**.
- Fallisce se il render vulkan fallisce, se il frame vulkan è completamente
  trasparente (il sintomo pre-fix), o se un solo byte diverge dall'oracle
  software.
- Host senza device Vulkan: `SKIP` esplicito, non un falso PASS;
  `CHRONON_GATE_REQUIRE_VULKAN=1` trasforma lo skip in fallimento (usato dalla
  lane CI `vulkan-gated-unit` in `.github/workflows/native-video.yml`).
- Misure locali: coverage 4020 px, 307200 byte identici, exit 0. Con le layer
  svuotate il gate esce 1 con «vulkan produced a fully transparent frame».

## Residuo

- Nessuno noto: il gate copre la parità sul raw pre-encode. Una eventuale
  verifica sulla **MP4 encoded** resta possibile ma è ridondante rispetto al
  raw (l'encoder è fuori dal percorso difettoso).

## Riferimenti
- `PROBLEMS.md` §Aperti (2026-10-19) — voce 2 rimanda a questo ticket.
- Scena che ha esposto il difetto:
  `ChrononTemplate/tools/showcases/emit_example_svg_recreation_plans.cpp`
  (ricreazione Example Svg del 2026-10-19).

**Stato finale:** ARCHIVIATO (2026-10-23) — RESOLVED (2026-10-20), nessun lavoro aperto.
