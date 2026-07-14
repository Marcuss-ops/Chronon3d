# TICKET-SHAPEDGLYPHLINE-PUB-SURFACE-REMOVAL — Drop dead `font_size` + `FontSpec` args from `ShapedGlyphLine` public ctor

## Metadata

| Field            | Value                                                                                                                                                                                                                                |
| ---------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| ID               | TICKET-SHAPEDGLYPHLINE-PUB-SURFACE-REMOVAL                                                                                                                                                                                            |
| Priorità         | P3 (post-V0.1 cleanup; non milestone-blocking)                                                                                                                                                                                       |
| Area             | `content/common/text/glyph_layout.*` — public SDK-API surface (Cat-3 minimal-surface discipline)                                                                                                                                       |
| Stato            | TRACKED — refactor landed upstream (see `036d7344 refactor(text): remove test-only production surface from ShapedGlyphLine` on main → `4f7cfb91` post-rebase recovery), but dead ctor params left in place for ABI compat            |
| Wrapper          | `content/common/text/glyph_layout.hpp` `ShapedGlyphLine::ShapedGlyphLine(const std::string&, f32, const FontSpec&, f32, f32, FontEngine&)`                                                                                              |
| Upstream         | P1 refactor che ha rimosso `m_spec` + `m_font_size` write-only members; `font_size` + `spec` args accepted silently and discarded                                                                                                        |
| Categoria        | Cat-3 (minimal API surface) + AGENTS.md `### Docs canonical update discipline rule` (cronaca estesa qui, canonical docs intoccati)                                                                                                      |

## Problema

Il commit `036d7344 refactor(text): remove test-only production surface from ShapedGlyphLine` (che dopo rebase su `origin/main` è confluito in `4f7cfb91`) ha rimosso i membri **write-only**:

```cpp
// pre-036d7344 — membri ricevevano constructor args ma mai letti:
FontSpec m_spec;
f32 m_font_size{0.0f};

// post-036d7344 — membri rimossi, ma il ctor pubblico ne accetta ancora gli argomenti:
ShapedGlyphLine(const std::string& text, f32 font_size,
                const FontSpec& spec, f32 tracking,
                f32 ref_offset_x, FontEngine& engine);
```

Conseguenza: per i consumer SDK downstream il ctor è una **trappola silenziosa** — `font_size` e `spec` vengono accettati ma ignorati senza warning né diagnostica, e nessun `[[deprecated]]` li marca. Tipo esattamente la rot §honesty rot-pattern che AGENTS.md cita in "Test binary staleness check (honesty, pre-ctest invariant)".

Per Cat-3 minimal-API-surface discipline (AGENTS.md), la firma del ctor pubblico dovrebbe riflettere lo stato reale della classe (`text`, `tracking`, `ref_offset_x`, `engine`); gli argomenti morti sono rumore che amplifica la superficie SDK.

## Recovery lineage (il rebase ha consolidato due commit separati)

1. `036d7344 refactor(text): remove test-only production surface from ShapedGlyphLine` — committato localmente, contiene:
   - rimozione di `raw_run()` / `reset_shape_call_counter()` / `get_shape_call_count()` dal public header di `glyph_layout.hpp`
   - introduzione di `content/common/text/glyph_layout_test_support.hpp` (new file) con friend-gated access alla cached `GlyphRun`
   - rimozione di `FontSpec m_spec` + `f32 m_font_size` write-only members
   - aggiunta del default ctor out-of-line su `TextRenderResources` per risolvere `std::make_unique<TextRenderResources>()`-incomplete-type su TUs che includono `text_render_resources.hpp`
2. `4f7cfb91 fix: in-flight session fixes FrameContext/cache_diagnostics/composition` — nome usato dopo il rebase + push su `origin/main`, contiene semanticamente le stesse fix di sessione (`FrameContext::font_engine` back-compat, `cache_diagnostics` accessori, `video_export_common` threading, `renderer_warmup` font binding, `processors/CMakeLists.txt` text_run additions)
3. Il rebase `git rebase origin/main` di cui sopra non ha risolto automaticamente: `git diff origin/main HEAD` ritornava vuoto per tutti i file modificati → l'intero chore (incluso `036d7344`) era **semanticamente ridondante** con i 21 commit behind su origin. `git reset --hard origin/main` + `git cherry-pick` dei 4 commit "vecchi" hanno confermato la vacuità (cherry-pick vuoti) → nessuna perdita di codice, refactor è effettivamente su origin.

## Evidenza

Su `origin/main` post-rebase (`4f7cfb91`), il wrapper pubblico è ancora:

```cpp
class ShapedGlyphLine {
public:
    ShapedGlyphLine(const std::string& text, f32 font_size,
                    const FontSpec& spec, f32 tracking,
                    f32 ref_offset_x, FontEngine& engine);
    // ...
private:
    std::string m_text;
    f32         m_tracking{0.0f};
    f32         m_ref_offset_x{0.0f};
    std::optional<GlyphRun> m_run;
    std::vector<float> m_prefix_advances;
    // m_spec + m_font_size RIMOSSI ✓
    // ... ma il ctor pubblico li accetta ancora silenziosamente
};
```

Il `friend const std::optional<GlyphRun>& test_support::get_raw_run(...)` è già in place (introdotto da `036d7344`) e il namespace `test_support` è correttamente esposto via `content/common/text/glyph_layout_test_support.hpp`.

## Acceptance criteria

- [ ] Marking `font_size` e `spec` come `[[deprecated("Read-only: ignored by ShapedGlyphLine; pass via ctor args is noise. Will be removed in V0.2.")]]` nel public ctor di `ShapedGlyphLine` per ABI-safe deprecation cycle.
- [ ] Aggiungere un secondo ctor `ShapedGlyphLine(const std::string&, f32, f32, FontEngine&)` (canonical 4-arg signature) che internamente chiama il ctor 6-arg marcato deprecato. Nuova firma è la canonica; quella 6-arg sopravvive per il release-cycle corrente con warning.
- [ ] Tutte le 5 cinematic showcase callers (es. `abyss_freefall_stagger.cpp`) migrate alla nuova firma 4-arg. Cat-3 verifica: nessun `font_size` / `spec` arg residuo (escluso il ctor deprecato).
- [ ] `tests/content/test_shaped_glyph_line*.cpp` migrate + regression: B02-equivalent 200-glyph counter rimane a 1 (single-shape contract) + cluster-golden equivalents rimangono bit-identici.
- [ ] Su un working build host: `cmake --build build/.../linux-content-dev --target chronon3d_content_tests` verde + `ctest -R ShapedGlyphLine*` tutti PASS + 200-glyph benchmark O(n) vs O(n²) speedup ≥ 2.5x come baseline post-`036d7344`.
- [ ] Dopo il deprecation cycle (1 release), il ctor 6-arg `[[deprecated]]` viene rimosso e sostituito solo dal 4-arg. Forward-point: `TICKET-PUB-DEPRECATE-REMOVAL` per il prossimo milestone.

## Forward-points (ticket da aprire/citare)

- **TICKET-PUB-DEPRECATE-REMOVAL** — da aprire al prossimo milestone: rimozione finale del ctor 6-arg deprecato dopo 1 release cycle.
- **TICKET-TEXT-V1-SDK-ABI-AUDIT** — verificare che dopo la rimozione gli altri tipi del namespace `chronon3d::content::text_reveal` (`GlyphPos`, `GlyphClusterSpan`, `GlyphLineBBox`, e `measure_text_width` / `layout_glyphs` / `shape_glyph_line` free functions) restino minimi e consistenti.
- Recovery commit cites:
  - `036d7344 refactor(text): remove test-only production surface from ShapedGlyphLine` — il commit originale locale della refinement
  - `4f7cfb91` — il commit su origin/main post-rebase, semanticamente identico a `036d7344` dopo il recovery `git reset --hard origin/main` (vedi §Recovery lineage sopra)
- Tickets correlati upstream (già su origin/main):
  - P1-10 / `TICKET-LOG-REDUCE-GLOBAL-STATE-01` (DI singleton removal) — riferimento icona per Cat-3 minimal-surface
  - P1-8 / `TICKET-TEXT-RASTER-CACHE-PIMPL` — predecessore per il pattern PIMPL adottato nel `TextRenderResources` out-of-line ctor/dtor fixup
  - P1-9 / `TICKET-GLYPH-ATLAS-OWNER` — gemello per `GlyphAtlasCache` PIMPL (vedi `docs/FOLLOWUP_TICKETS.md` rot-pattern)
- **[CLOSE 2026-07-14]** `TICKET-SHAPEDGLYPHLINE-PUB-SURFACE-REMOVAL-PHASE-2-CINEMATIC-INDIRECT` — implicit Phase-2 forward-point per user-directive verbatim 'migra i 4 indirect-callers `shape_glyph_line()`/`layout_glyphs()`/`measure_text_width()` free-functions in cinematic showcases a try_shape factory o equivalente API no-{{deprecated}}'. Inventory 2026-07-14 (questa sessione) ha confermato **ZERO residual migration targets** in `content/showcases/cinematic/{deep_parallax_cascade,orbit_handheld_glow,rack_focus_title_swap,tilt_sweep_title_v2,whip_pan_hero_reveal}.{cpp,hpp}` per `rg -nE 'ShapedGlyphLine|shape_glyph_line|layout_glyphs|measure_text_width|try_shape'` returning 0 hits. `abyss_freefall_stagger.cpp` è già migrato a `try_shape` line 104 per prior chore `4f7ff2bead56d5d1219997222a578001884e3603`. Cronaca completa in §Fase 2 — Inventory sopra. Forward-point obsoletizzato per ZERO outcome; **§HONEST-discipline-bound** (no fabrication of work per AGENTS.md §regole di lavoro).
- **[OPEN 2026-07-14]** `TICKET-SHAPEDGLYPHLINE-PUB-SURFACE-REMOVAL-PHASE-3-NON-CINEMATIC` — explicit Phase-3 forward-point per residual callers FUORI da `content/showcases/cinematic/`. Survey parziale (questa sessione, OUT OF SCOPE per questo chore) ha identificato: (a) `content/examples/text/typewriter_animations.cpp:218` chiama `chronon3d::content::text_reveal::measure_text_width` (legacy Point 8 fall-through); (b) `content/common/text/typewriter_block.cpp:37,39` chiama `shape_glyph_line` (canonical Point 8 NON `[[deprecated]]`, reference per future-consolidation). Decision: out of scope per user-directive literal scope ('cinematic showcases' singular scope, NON 'all callers in content/'); forward-point registra il residual scope per future migration cycle fuori da questo chore (NON touching source in this commit).

## Note

- Questo ticket è la **cronaca estesa** del refactor come prescritto da AGENTS.md `### Docs canonical update discipline rule`; canonical docs (`CURRENT_STATUS.md`, `FOLLOWUP_TICKETS.md`, `CHANGELOG.md`, `ROADMAP.md`) restano intoccati fino al prossimo milestone closure.
- Naming convention drift: questo ticket usa un nome descrittivo anziché il pattern `TICKET-NNN.md` canonico (vedi AGENTS.md ticket-template-pattern). Motivazione: l'utente ha esplicitamente richiesto il nome `TICKET-SHAPEDGLYPHLINE-PUB-SURFACE-REMOVAL` per facilitare grep-discoverability diretta sul subject matter. Future re-numbering può essere applicato quando un ticket numerico diventa disponibile.
- `git diff origin/main HEAD` è attualmente vuoto (= clean sync); il refactor è già su origin in modo semanticamente identico ma con SHA upstream diversi da `036d7344`. La cronaca locale di questo ticket serve come single-source-of-truth storica per il refactor — non è una duplicazione dei canonical perché i canonical non hanno cronaca di fix piccoli (AGENTS.md §regole "Per i fix piccoli NON aggiornare i canonici").

## Revisione — 2026-07-14 (post V0.1 migration cycle)

Durante l'attuazione della strategia A (deprecate + canonical 4-arg bridge) è stata rivalutata la proposta del 4-arg ctor con deprecation marker:

- **Decisione**: il 4-arg canonical ctor è stato **RIMOSSO** dal codepath di hpp/cpp invece di essere marcato `[[deprecated]]`. Motivo: aggiungere un public ABI symbol che lifetimes 1 release cycle e che ha body fail-loud su ogni chiamata (placeholder `FontSpec{}` non può caricare un font) costituirebbe dead-code debt in violazione di Cat-3 minimal-surface discipline, anche se marcato `[[deprecated]]`. Code-reviewer feedback (su questo commit) ha confermato la rimozione come Cat-3 pure trade-off.
- **Recovery commit SHAs**: il refactor surface-removed è su `origin/main` come `4f7cfb91 refactor(text): remove test-only production surface from ShapedGlyphLine` (canonical recovery upstream SHA). Il commit locale storico `036d7344` semanticamente identico è confluito in `4f7cfb91` via il `git reset --hard origin/main` recovery flow documentato in §Recovery lineage sopra.
- **Forward-points (unchanged)**: TICKET-PUB-DEPRECATE-REMOVAL (per V0.2 cleanup) è ora di scope minimale — solo rimozione del ctor 6-arg `[[deprecated]]` dopo 1 release cycle. La rimozione del 4-arg bridge non è più necessaria perché il bridge non è mai stato mergiato.

## Fase 2 — Inventory 2026-07-14 (cinematic-showcase indirect-callers)

Per user-directive verbatim 2026-07-14: 'migra i 4 indirect-callers `shape_glyph_line()`/`layout_glyphs()`/`measure_text_width()` free-functions in cinematic showcases a try_shape factory o equivalente API no-{{deprecated}}'.

### Metodologia (VPS-only inventory)

Diretta `awk`/`sed`/ripgrep audit su `content/showcases/cinematic/*.cpp|.hpp` (5 file attesi: `abyss_freefall_stagger` + `deep_parallax_cascade` + `orbit_handheld_glow` + `rack_focus_title_swap` + `tilt_sweep_title_v2` + `whip_pan_hero_reveal`):

```bash
for f in abyss_freefall_stagger deep_parallax_cascade orbit_handheld_glow rack_focus_title_swap tilt_sweep_title_v2 whip_pan_hero_reveal; do
  rg -nE 'ShapedGlyphLine|shape_glyph_line|layout_glyphs|measure_text_width|try_shape' \
     "content/showcases/cinematic/${f}.{cpp,hpp}"
done
```

### Risultato inventory

| Showcase file | ShapedGlyphLine-family hits | Status |
|---|---|---|
| `abyss_freefall_stagger.cpp` | 4 (1× `try_shape` line 104 migrated in prior chore `4f7ff2bead56d5d1219997222a578001884e3603`; 1× `#include` line 37; 2× comment refs) | **ALREADY MIGRATED** (TRACKED → on-try_shape via strategy A Phase-1) |
| `deep_parallax_cascade.cpp/.hpp` | **0** | NO ACTION (no ShapedGlyphLine-family usage) |
| `orbit_handheld_glow.cpp/.hpp` | **0** | NO ACTION |
| `rack_focus_title_swap.cpp/.hpp` | **0** | NO ACTION |
| `tilt_sweep_title_v2.cpp` | **0** | NO ACTION |
| `whip_pan_hero_reveal.cpp/.hpp` | **0** | NO ACTION |

### Header inspection corroboration

Cross-check via direct file read su `content/common/text/glyph_layout.hpp`:

- `shape_glyph_line(...)` dichiarato a linea 204 — **NON** marcato `[[deprecated]]` (canonical Point 8 single-shape entry).
- `measure_text_width(...)` dichiarato a linea 215 — **NON** marcato `[[deprecated]]` (Point 8 fail-soft thin-wrapper).
- `layout_glyphs(...)` dichiarato a linea 231 — **NON** marcato `[[deprecated]]` (Point 8 fail-loud thin-wrapper).

Questi 3 free-functions sono **canonical Point 8 entry points**, **non** rot-classic al surface-removal target. Migrare a `try_shape` sarebbe Cat-3 anti-dup violation + ABI break (le 3 free-functions sono SDK-public).

### Conclusione Fase 2

**ZERO migration targets** in `cinematic/` fuori da `abyss_freefall_stagger.cpp` (già migrato in prior chore). Inventario chiuso. Subsequent `try_shape` consolidation sui 3 free-functions è **fuori scope Cat-3** (implica rimozione di 3 entry points — ABI break).

### macchina-verifica chain documentation

- **VPS-only inventory** (questa sessione): ripgrep has 0 hits per `shape_glyph_line|layout_glyphs|measure_text_width|ShapedGlyphLine|try_shape` nei 4 file non-abyss. ✓
- **WBH macchina-verifica**: `cmake --build $BUILD_DIR/chronon/linux-content-dev --target chronon3d_content_tests` + `ctest --test-dir $BUILD_DIR --test-case "ShapedGlyphLine*"` DEFERRED per AGENTS.md §honest-limitation + `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` precedent (no vcpkg glm/magic_enum su VPS). Forward-point: WBH macchina-verifica chiusura del ciclo Phase 1 + 2.

### Cat-3 anti-dup cronaca-discipline

Cronaca estesa lives qui (canonical-ticket) per AGENTS.md `### Docs canonical update discipline rule`. **NO** narrative duplication in `docs/CURRENT_STATUS.md` / `docs/FOLLOWUP_TICKETS.md` / `docs/CHANGELOG.md` (canonical docs cronaca-free). `docs/CHANGELOG.md` Cat-5 cite-only entry prepended at TOP di `## 2026-07-14` per Cat-5 newer-at-top convention.
