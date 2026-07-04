# TICKET-M1.5#11-SEQUENCE — text_rasterizer_render.cpp classification + extraction (single step)

> Stato corrente: [`docs/CURRENT_STATUS.md`](CURRENT_STATUS.md).
> Ticket relativo: [`TICKET-M1.5#9-SEQUENCE`](TICKET-M1.5#9-SEQUENCE.md) (legacy text pipeline elimination — Step 5 deletes rasterizer TU).

## Scopo

Refactor di `src/backends/text/text_rasterizer_render.cpp` (1064 LOC) in 4 fasi mentali:
1. **Classificazione** — distinguere le parti riusabili dalle parti legacy.
2. **Estrazione** — preservare le utility BL+FreeType in moduli con nomi espliciti.
3. **Eliminazione** — il resto viene cancellato con M1.5#9 (P1-LEGACY-TEXT-PIPELINE).
4. **Anti-duplicazione** — rispetto rigoroso del vincolo "nessuna duplicazione rispetto a M1.5#6+#7".

## Decisioni di classificazione

### Da migrare (con M1.5#9)

| Concetto | Destinazione finale |
|---|---|
| `rasterize_text_to_bl_image(...)` | M1.5#9 Step 5 wholesale delete |
| Cache raster globale (`text_rasterizer_cache.cpp` siblings) | M1.5#7-FULL-SPLIT per-session migration |
| Conversione `TextShape → layout` | Already migrated to text_layout_engine.cpp canonical |

### Da conservare (preserved in moduli espliciti)

| Utility | Modulo di destinazione | LOC |
|---|---|---|
| `apply_text_style` + i due thin wrappers | **`blend2d_glyph_conversion.cpp`** | ~70 LOC declarations |
| `HbToBlGlyphRun` struct + `from(...)` static factory | **`blend2d_glyph_conversion.cpp`** | ~50 LOC moved |
| `FT_Outline_Funcs` callbacks + `FT_Outline_Decompose` dispatch | **`freetype_outline_conversion.cpp`** | ~80 LOC moved |

### Da conservare LOCAL (deleted con M1.5#9)

| Cache | Razionale del keep-local |
|---|---|
| `Blend2DResources` (anon-namespace, `get_face()`) | Duplicates M1.5#7 `BLFontFaceCache`. ABI-stable signature `rasterize_text_to_bl_image(..., resolver, ...)` non consente migrazione (AGENTS.md Cat-5). |
| `FtGlyphPathBuilder::load_face` (anon-namespace) | Duplicates M1.5#7 `FreeTypeFaceCache`. `build_path` body estratto, ma `load_face` + `mutex` restano per protezione race-condition `FT_Set_Pixel_Sizes` vs `FT_Load_Glyph`/`FT_Outline_Decompose`. |

## Moduli creati

### `src/backends/text/blend2d_glyph_conversion.{hpp,cpp}` (M1.5#11)

Namespace: `chronon3d::`. Symbols:
- `apply_text_style(BLContext&, StyleOp, std::optional<Fill>, Color, ox, oy, w, h)` — unified fill/stroke applicator, deduplica `apply_text_fill_style`/`apply_text_stroke_style` mirror images (~180 LOC saved pre-M1.5#11).
- `apply_text_fill_style(BLContext&, TextStyle&, Color, ...)` — thin wrapper (legacy call-site signature).
- `apply_text_stroke_style(BLContext&, TextStyle&, Color, ...)` — thin wrapper.
- `struct HbToBlGlyphRun` + `static HbToBlGlyphRun::from(const PlacedGlyphRun&, BLFontFace&, float)` — HarfBuzz→Blend2D glyph-data converter with design-unit scaling (`upem / font_size`).

`using-declarations` scoped inside `namespace chronon3d { namespace { ... } }` per il pattern `text_processor_helpers.hpp`.

Anti-duplicazione: ZERO logica di cache in questo modulo. Font-face I/O appartiene a M1.5#7's `BLFontFaceCache` (per-session) — non riesportato qui.

### `src/backends/text/freetype_outline_conversion.{hpp,cpp}` (M1.5#11)

Namespace: `chronon3d::`. Symbols (sotto `CHRONON3D_ENABLE_TEXT` guard):
- `[[nodiscard]] BLPath convert_ft_outline_to_bl_path(FT_Face, const PlacedGlyphRun&, float ox, float oy)` — pure outline-decode utility: `FT_Load_Glyph` + `FT_Outline_Decompose` → BLPath. Y-axis mirror (font-up → BL-down). Pixel→em scaling `kScale = 1/64`.

Anti-duplicazione: ZERO caching, ZERO face I/O. FT_Face arrivata via parametro, lifetime + locking gestiti dal chiamante. M1.5#7's `FreeTypeFaceCache` resta il canonical face I/O; M1.5#7's `GlyphOutlineBuilder::build_path(FT_Face, u32, ...)` resta canonical per-glyph — questo modulo è canonical BATCHED multi-glyph iteration (per-`PlacedGlyphRun`), usato dallo stroke path che richiede fuse-contextual Arabic/Devanagari rendering.

## File modificati

- `src/backends/text/text_rasterizer_render.cpp` — rimosse ~180 LOC di utility duplication (3 apply_text_* + HbToBlGlyphRun + build_path body); 2 callsites `ft_path.build_path(...)` re-shaped a `lock_guard(ft_path.mutex) + convert_ft_outline_to_bl_path(ft_path.ft_face, ...)`.

## File non modificati

- `include/chronon3d/backends/text/text_rasterizer_utils.hpp` — ABI pubblico invariato.
- `src/backends/software/processors/text/software_text_processor.cpp` — unica production caller di `rasterize_text_to_bl_image`, signature stabile.
- `src/backends/text/text_rasterizer_cache.cpp` + `text_rasterizer_ink.cpp` — già estratti in precedenza (text_rasterizer_ink.cpp compute_text_ink_bbox + draw_text_debug_overlays; text_rasterizer_cache.cpp hash/lookup/store/clear).

## Vincoli architetturali

| Vincolo | Status |
|---|---|
| ZERO new public API | ✓ (headers interni a `src/backends/text/`, NON in `include/chronon3d/`) |
| ZERO new singleton/registry/cache | ✓ (moduli puri, no caching) |
| NO duplicazione vs M1.5#6+#7 | ✓ (zero cache locality nei moduli nuovi; legacy cache resta solo dove cancellata con M1.5#9) |
| ABI pubblico `rasterize_text_to_bl_image` invariato | ✓ (signature unchanged) |
| AGENTS.md Cat-3 (Rimozione percorsi legacy) freeze-compliant | ✓ |
| Wrap_push.sh GATE-MNT-01 push-side gate | ✓ (pre-push) |
| Doc sync (CHANGELOG + FOLLOWUP_TICKETS + CURRENT_STATUS) nello stesso commit | ✓ |

## Verifica build

```bash
commit=$(git rev-parse HEAD)
cmake --build build --target chronon3d_backend_text -- -j2
cmake --build build --target chronon3d_sdk_impl     -- -j2   # downstream
bash tools/check_doc_sync.sh                          # gate #7
bash tools/check_legacy_text_pipeline.sh              # gate (M1.5#9 whitelist)
```

## Sequenza di esecuzione (1 commit, atomic)

1. `write_file` x4: `blend2d_glyph_conversion.{hpp,cpp}` + `freetype_outline_conversion.{hpp,cpp}` (verbatim-extracted bodies).
2. `write_file`: `text_rasterizer_render.cpp` slimmed version.
3. `str_replace`: `src/backends/text/CMakeLists.txt` — add 2 nuovi TU a `chronon3d_backend_text` OBJECT.
4. Doc sync: `CHANGELOG.md` (Luglio 2026 reverse-chrono entry) + `FOLLOWUP_TICKETS.md` (Recently-closed) + `CURRENT_STATUS.md` (progress row) + NEW `docs/tickets/TICKET-M1.5#11-SEQUENCE.md`.
5. Build verify (sopra).
6. Code-reviewer parallel with build.

## Step status

> **Status:** Done (TICKET-M1.5#11-SEQUENCE complete, single atomic commit, build green, code-reviewer APPROVED).

## Riferimenti

- [`docs/CURRENT_STATUS.md`](CURRENT_STATUS.md) — progress row M1.5#11.
- [`docs/FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md) — Recently-closed entry.
- [`docs/CHANGELOG.md`](CHANGELOG.md) — Luglio 2026 reverse-chrono entry.
- [`docs/tickets/TICKET-M1.5#9-SEQUENCE.md`](TICKET-M1.5#9-SEQUENCE.md) — legacy pipeline deletion (downstream consumer of this ticket).
- [`include/chronon3d/backends/text/text_render_resources.hpp`](../../include/chronon3d/backends/text/text_render_resources.hpp) — M1.5#7 canonical ownership.
- [`AGENTS.md`](../../AGENTS.md) — Cat-3 freeze + canonical ownership invariants.
