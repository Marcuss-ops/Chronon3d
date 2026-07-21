# CapCut Reference Corpus — Blessed PNG policy

**Status**: OPEN (corpus skeleton committato, blessed PNGs deferred-PR-review)
**Ticket**: [TICKET-CAPCUT-REFERENCE-CORPUS](../../docs/tickets/TICKET-CAPCUT-REFERENCE-CORPUS.md)

## Policy

This directory holds **blessed** PNG exports from CapCut, used as gold-standard
references for the `chronon3d_capcut_parity_tests` parity test. The policy is
**strict**:

- **`reference/` is blessed-only**: PNGs here are produced manually from CapCut
  (same font + same resolution + same text + same dimension + same tracking +
  same line spacing + background trasparente + PNG lossless). Committed ONLY
  via PR review esplicito che dimostri blessed-ness (NON auto-generato).
- **`current/` is regenerable**: contains Chronon3D rendered outputs that the
  parity test compares against `reference/`. **Gitignored** (see local
  `.gitignore` per subdir). Free to regenerate, never blessed.
- **No auto-substitution**: NEVER overwrite `reference/` with Chronon3D output,
  even on `CHRONON3D_UPDATE_GOLDENS=1`. The update mode for Chronon3D goldens
  lives in `tests/golden/` (different policy, different intent).
- **PR review required**: adding a new PNG to `reference/` requires explicit
  review that confirms the PNG was exported from CapCut (or comparable tool)
  and matches the input spec verbatim.

## Struttura

```
tests/reference/capcut/
├── README.md                 (this file)
├── CMakeLists.txt            (chronon3d_add_test_suite registration)
├── test_capcut_parity.cpp   (parity test stub)
├── static/
│   ├── .gitignore            (current/ ignored, reference/ tracked-only-post-PR)
│   ├── reference/            (blessed static PNGs — empty until PR-review seed)
│   │   └── .gitkeep
│   └── current/              (regenerable outputs — gitignored)
│       └── .gitkeep
├── subtitles/
│   ├── .gitignore
│   ├── reference/            (blessed subtitle PNGs)
│   │   └── .gitkeep
│   └── current/
│       └── .gitkeep
└── effects/
    ├── .gitignore
    ├── reference/            (blessed effect PNGs)
    │   └── .gitkeep
    └── current/
        └── .gitkeep
```

## Convenzione di naming

`<area>_<font>_<test-id>.png` — esempi:
- `static/static_inter_bold.png` — static text, Inter Bold, base case
- `static/static_noto_sans_caption.png` — static text, Noto Sans, caption size
- `subtitles/subtitle_karaoke_word_highlight.png` — karaoke word highlight
- `subtitles/subtitle_safe_area_lower_third.png` — safe-area lower-third
- `effects/effect_glow_pulse.png` — glow pulse animation frame
- `effects/effect_text_on_path.png` — text-on-path animation frame

`<test-id>` deve essere univoco nel proprio `<area>/reference/` dir.

## How to seed a blessed reference (PR review workflow)

1. **Esporta da CapCut**: usa lo stesso identico file font, stessa risoluzione
   (es. 1920×1080 per landscape, 1080×1920 per portrait), stesso testo, stessa
   dimensione, stesso tracking, stesso line spacing. Background trasparente,
   PNG lossless (no JPEG/HEIC).
2. **Posiziona il file**: copia in `tests/reference/capcut/<area>/reference/<naming>.png`
3. **Commit message esplicito**: subject + body che cita il tool di export
   (es. "seed blessed reference static_inter_bold.png from CapCut Desktop vX.Y.Z
   with font Inter-Bold.ttf + 1920x1080 + text 'CapCut' + 96pt + tracking 0 + LH 1.2
   + transparent BG + lossless PNG").
4. **PR review**: il reviewer verifica che il PNG sia genuinamente blessed
   (NON auto-generato da Chronon3D) controllando i metadata del PNG + facendo
   diff visual con un export CapCut fresco on-the-spot.

## §Blessing Workflow (who, how, when)

Per evitare che la policy "blessed-only" sia policy vuota, ecco il workflow
**completo** per aggiungere un nuovo reference PNG al corpus. **NON** seguire
questo workflow significa rigenerare accidentalmente un reference blessed
(rot-class che degrada la confidence nel parity test).

### Chi può blessare

- **Maintainer design-system** (1-2 persone designate nel team) — sono gli
  unici che possono approvare un PR che aggiunge un file a `reference/`.
  Lista in `CODEOWNERS` (da creare) o nel README del team.

### Come si blessa (workflow)

1. **Designer / maintainer** apre CapCut (Desktop vX.Y.Z) con il preset/template
   originale (es. "Bold Title" preset per `static/static_inter_bold.png`).
2. **Designer** esporta in PNG lossless (no JPEG/HEIC/WEBP), background
   trasparente, risoluzione target (1920×1080 / 1080×1920 / etc.).
3. **Designer** esegue `sha256sum` sul PNG + copia il checksum nel PR body.
4. **Designer** apre PR con subject:
   ```
   seed blessed reference <area>_<font>_<test-id>.png from CapCut
   ```
   + body che cita:
   - Tool + version: "CapCut Desktop vX.Y.Z"
   - Font: "Inter-Bold.ttf" (sha256: `xxxx`)
   - Risoluzione: "1920×1080"
   - Testo: "'CapCut'" (o specifico)
   - Dimension: "96pt"
   - Tracking: "0"
   - Line height: "1.2"
   - Background: "transparent"
   - Encoding: "PNG lossless (filter=None, compression=0)"
   - Checksum PNG: `sha256:xxxx`
5. **Reviewer design-system** verifica il PR:
   - Checksum corrisponde al file committato
   - Subject cita TUTTI i parametri (no param mancanti)
   - Il PNG NON è stato auto-generato da Chronon3D (verifica confrontando
     con un export CapCut fresco on-the-spot)
6. **Reviewer** approva e merge.

### Quando NON si blessa

- Mai durante un normale dev cycle (anche se un test fallisce per blessed PNG
  mancante → forward-point PR-review separato, NON blessing in dev commit).
- Mai durante un fix rot (TICKET-GRAPHICS-SHAPE-STYLE-ROT, TICKET-SYSTEMIC-NAMESPACE-ROT
  → sono source code rot, NON richiedono blessed PNGs).
- Mai come "fast path" per chiudere un parity test FAIL (se il parity test
  fallisce, è un bug Chronon3D, NON un motivo per blessare un PNG blessed).

## How to rigenerare i reference (script manuale — NON incluso)

Lo script di rigenerazione **NON** è incluso in questo repository per policy
("nessuna auto-sostituzione di reference blessed"). Per rigenerare:

1. Apri CapCut con il preset/template originale.
2. Esporta in PNG lossless.
3. Segui il workflow PR review sopra.

## Soglie CapCut-grade parity (verdict §Fase 9)

| Metrica | Soglia | Implementazione |
|---|---|---|
| `baseline err` | ≤ 1 px | inline helper `compute_baseline_error` |
| `bbox err` | ≤ 2 px per lato | inline helper `compute_bbox_error` |
| `SSIM-on-ROI` | ≥ 0.95 | riuso `chronon3d::test::compute_ssim` (image_diff.hpp) |
| `changed_pixel_ratio` (ROI) | ≤ 5% | riuso `chronon3d::test::compare_framebuffers` |
| `missing_glyphs` | 0 | inline helper `count_missing_glyphs` |
| `cut_text` | false | inline helper `detect_cut_text` (alpha-bbox touches framebuffer edge) |

## Forward-points

Vedi [TICKET-CAPCUT-REFERENCE-CORPUS §Forward-points](../../docs/tickets/TICKET-CAPCUT-REFERENCE-CORPUS.md#forward-points).
