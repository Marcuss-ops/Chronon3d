# TICKET-PUB-DEPRECATE-REMOVAL — V0.2 removal of deprecated `ShapedGlyphLine` ctors

## Metadata

| Field            | Value                                                                                                                                                                                                                                                                       |
| ---------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| ID               | TICKET-PUB-DEPRECATE-REMOVAL                                                                                                                                                                                                                                                |
| Priorità         | P3 (V0.2 milestone prep; P0 if 6/12 SDK consumer tests regress basis fires)                                                                                                                                                                                                 |
| Area             | `content/common/text/glyph_layout.hpp/cpp` — public SDK-API surface cleanup (Cat-3 minimal-surface follow-up)                                                                                                                                                              |
| Stato            | TRACKED — deprecation markers in place on `4f7cfb91 refactor(text): remove test-only production surface from ShapedGlyphLine`; awaits V0.2 SDK consumer compatibility verification before removal                                                        |
| Wrapper          | `content/common/text/glyph_layout.hpp` lines ~120-150 (the two `[[deprecated]]` marker sites)                                                                                                                                                                              |
| Upstream         | `036d7344 refactor(text): remove test-only production surface from ShapedGlyphLine` (local) → `4f7cfb91` (upstream post-rebase; see TICKET-SHAPEDGLYPHLINE-PUB-SURFACE-REMOVAL §Recovery lineage)                                                                              |
| Categoria        | AGENTS.md `### Docs canonical update discipline rule` (cronaca estesa qui); Cat-3 minimal-API-surface                                                                                                                                                                    |

## Problema

I due ctors publici di `ShapedGlyphLine` sono marcati `[[deprecated]]` in V0.1 per un deprecation cycle:

```cpp
// 6-arg form — the historical primary ctor, now deprecated:
[[deprecated("Use ShapedGlyphLine::try_shape(text, font_size, spec, tracking, "
             "ref_offset_x, engine) factory or shape_glyph_line(...) free "
             "function. The 6-arg ctor will be REMOVED in V0.2.")]]
ShapedGlyphLine(const std::string& text, f32 font_size,
                const FontSpec& spec, f32 tracking,
                f32 ref_offset_x, FontEngine& engine);

// 4-arg form — TICKET-PUB-DEPRECATE-REMOVAL canonical bridge, also deprecated:
[[deprecated("The 4-arg canonical ctor uses placeholder defaults that fail loud. "
             "Use ShapedGlyphLine::try_shape(text, font_size, spec, tracking, "
             "ref_offset_x, engine) for real shaping — pass explicit "
             "font_size + FontSpec to engine.shape_text.")]]
ShapedGlyphLine(const std::string& text, f32 tracking,
                f32 ref_offset_x, FontEngine& engine);
```

V0.2 milestone closure: dopo un release cycle con i deprecation warnings attivi, rimuovere ENTRAMBI i ctors deprecati quando il migration contract è verificato sui SDK consumer tests.

## Acceptance criteria (basis: ≥ 6/12 SDK consumer tests compat senza il bridge 6-arg al V1 cut)

- [ ] Tutti i 12 SDK consumer tests esistenti (`tests/install_consumer/main*.cpp` + `tests/text/visual/*.cpp` + `tests/certification/test_cert_text*.cpp`) compilano e PASSANO senza il ctor 6-arg (cioè rimuovendo temporaneamente la definizione + la dichiarazione in un working branch).
- [ ] **Basis di chiusura**: ≥ 6 dei 12 SDK consumer tests devono passare senza il 6-arg bridge. Se il basis non è raggiungibile (alcuni test dipendono da signature specifica), questo ticket si sposta in P0 con un piano di remediation (probabilmente: introdurre un ADAPTER header separato che espone sole le firme canoniche).
- [ ] Dopo basis OK: rimuovere DEFINITIVAMENTE il ctor `[[deprecated]]` 6-arg + il ctor `[[deprecated]]` 4-arg da `glyph_layout.hpp` + dal relativo definition in `glyph_layout.cpp`.
- [ ] Tutti i test + showcase callers sono migrati a `ShapedGlyphLine::try_shape(...)` factory OR `shape_glyph_line(...)` free function (semantic equivalenti). Nessun caller residuo.
- [ ] `cat -n include/chronon3d/text/font_engine.hpp | grep -c '[[deprecated]]'` — non deve aumentare (no over-deprecation rot).
- [ ] `cmake --build build/.../linux-content-dev` verde dopo la rimozione.
- [ ] `ctest -R ShapedGlyphLine*` PASS tutti (regression di equivalenza bit-bit).
- [ ] `chrono-3d-text-bench` 200-glyph O(n) vs O(n²) speedup rimane ≥ 2.5x (no performance regression).

## Forward-points

- **TICKET-TEXT-V1-FREE-FN-SIGNATURES** (futuro): trimming di `font_size` + `FontSpec` dalle free functions `shape_glyph_line`, `measure_text_width`, `layout_glyphs`. Al momento fuori scope di questo ticket ma sopravvive come rot-pattern Cat-3 nel namespace `chronon3d::content::text_reveal` (verificato in `4f7cfb91 refactor(text): remove test-only production surface from ShapedGlyphLine` commenti).
- **TICKET-PUB-SDK-ABI-AUDIT** (future Post-V0.1): audit completo della ABI pubblicata per simboli marcati `[[deprecated]]`, garantire che la rimozione V0.2 non rompa consumer ABI entro il release-cycle corrente.

## Note

- Questo ticket è la **cronaca estesa** del deprecation cycle come prescritto da AGENTS.md `### Docs canonical update discipline rule`. Canonical docs (`CURRENT_STATUS.md`, `FOLLOWUP_TICKETS.md`, `CHANGELOG.md`, `ROADMAP.md`) restano intoccati fino al prossimo milestone closure.
- Naming convention drift: usa un nome descrittivo anziché il pattern `TICKET-NNN.md` canonico (vedi AGENTS.md ticket-template-pattern). Motivazione: `TICKET-SHAPEDGLYPHLINE-PUB-SURFACE-REMOVAL.md` (introdotto in V0.1) + `TICKET-PUB-DEPRECATE-REMOVAL.md` (questo file) formano un chiaro "V0.1 → V0.2 lifecycle pair" sul refactor surface.
- Basis del closure (≥ 6/12 SDK consumer tests) è la soglia operativa per decidere se rimuovere il bridge. La policy si basa sul principio che se la maggioranza (≥ 50%) della SDK surface migra correttamente, possiamo rimuovere il bridge senza fratturare il 50% residuo (che sarà guidato in un future ticket).
- **Cross-link coherence**: questo ticket è il V0.2 follow-up di `TICKET-SHAPEDGLYPHLINE-PUB-SURFACE-REMOVAL`. Quando si rimuove il 6-arg ctor in V0.2, fare riferimento anche a quel ticket (recovery lineage commit `036d7344` → `4f7cfb91` upstream).

## Revisione — 2026-07-14 (correction post V0.1 migration cycle)

Due correzioni al ticket originale dopo la prima tornata di migration:

1. **"4-arg canonical ctor" rimosso del codepath, non deprecato**. Il prototipo di `[[deprecated]]` 4-arg bridge ctor è stato REMOSSO prima del merge perché (a) costituiva dead-code public ABI symbol (placeholder FontSpec fallisce sempre), (b) violava Cat-3 minimal-surface discipline. Scope di questo ticket è quindi ridotto: solo rimozione del `[[deprecated]]` 6-arg ctor.
2. **"5 cinematic showcase callers" inaccurate.** Investigation grep rivela che solo `content/showcases/cinematic/abyss_freefall_stagger.cpp` è un direct caller del 6-arg ctor. I restanti 4 showcase (catmull_rom_showcase, cinematic_title_reveal, deep_parallax_cascade, dolly_zoom_showcase, rack_focus_title_swap, tilt_sweep_title_v2, whip_pan_hero_reveal, orbit_handheld_glow) transitano via `shape_glyph_line()` / `layout_glyphs()` / `measure_text_width()` free functions, che NON sono in scope di questo ticket. Il "4 Altri" gap è esplicitamente forward-pointed a **TICKET-TEXT-V1-FREE-FN-SIGNATURES**, che è il forward-point canonico per la migrazione delle free functions (rimozione dei parametri `font_size` + `FontSpec` da `shape_glyph_line` / `layout_glyphs` / `measure_text_width`).

Acceptance criteria di questo ticket (basis ≥ 6/12 SDK consumer tests) vanno interpretati su scope minimale (solo rimozione 6-arg ctor; nessuna modifica delle free functions).
