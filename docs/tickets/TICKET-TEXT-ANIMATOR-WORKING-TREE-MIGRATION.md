# TICKET-TEXT-ANIMATOR-WORKING-TREE-MIGRATION — Pre-existing build rot resolution (TextAnimator legacy → canonical animated_text + keyframes)

## Stato: DONE (2026-07-14)

## Problema

Il branch `main` presentava un **pre-existing build rot** (non derivante dal refactor P1-10 in working tree): il legacy fluent DSL `TextAnimator` era stato rimosso in commit precedenti (rot sostituito da `TextAnimatorSpec` + `Animator` authoring façade), ma 7 file in `include/chronon3d/presets/scenes/` + `examples/bench_corpus/bench_corpus_scenes.cpp` continuavano a chiamarlo, bloccando la build con ~30 errori di compilazione.

Mancavano inoltre altri 4 fix puntuali:
- `TextSpec::position` Vec3 rimosso in favore di `placement: TextPlacement` (sub-area ii di TICKET-TEXT-LEGACY-POSITION-ROT closure)
- `StarParams` ha cambiato signature (rimosso `.size`, ora `.center/.points/.inner_radius/.outer_radius`)
- `make_chronon_glow_final()` ritorna `Composition` ma 2 lambda richiedono `Scene` signature
- `CompositionDescriptor` ha ordine di campi `id, category, ..., factory` ma le 15 entry del registry `register_bench_corpus_compositions()` usavano `.factory` PRIMA di `.category` → designator-order violation C++20

## Soluzione Confine

Atomic Cat-3 minimal-surface chore (single commit candidate):
1. `include/chronon3d/presets/scene_presets.hpp:71-74` — `tp.position = pos` sostituito da `tp.placement = TextPlacement{TextPlacementKind::Absolute, Vec2{pos.x, pos.y}}`.
2. `examples/bench_corpus/bench_corpus_scenes.cpp`:
   - B03 + B11 lambda: aggiunto `.evaluate(ctx.frame)` su `make_chronon_glow_final(p)` per convertire `Composition` → `Scene`.
   - B07 Inner C `.star(...)` initializer: campo `.size` rimosso, aggiunti `.center/.points/.inner_radius/.outer_radius/.color`.
   - 15 entry `register_bench_corpus_compositions()`: designator order fix `.factory/.category` → `.category/.factory`.
   - B02 typewriter lambda: `TextAnimator ta; ... ta.build(s, "typewriter")` → `s.layer("typewriter", [&typewriter_text](LayerBuilder& l) { l.animated_text(...).commit(); l.opacity_anim().key(...) })` (capture + namespace + canonical animated_text API + layer-level keyframes).
3-7. 5 file `include/chronon3d/presets/scenes/{saas_intro, kinetic_title, depth_product_reveal, apple_style_hero, saas_intro_premium}.hpp`: stessa conversione `TextAnimator` → `s.layer(name, [](LayerBuilder& l) { l.animated_text(...).commit(); auto& op = l.opacity_anim(); op.key(...); ... })`.

Pattern canonico risultante (verificato in `examples/getting_started/main.cpp:50` + `content/compositions/chronon_glow_final.cpp:174`):
```cpp
s.layer("title", [&](LayerBuilder& l) {
    l.animated_text("title_inner", TextRunSpec{
        .text = detail::text_preset("Build Faster", 96.0f, 900,
            {1.0f, 1.0f, 1.0f, 1.0f},
            TextAlign::Center, {800.0f, 110.0f}, {0.0f, 0.0f, 0.0f},
            detail::default_font())
    }).commit();

    auto& op = l.opacity_anim();
    op.key(Frame{0},  0.0f, EasingCurve{Easing::Hold});
    op.key(Frame{3},  0.0f, EasingCurve{Easing::OutCubic});
    op.key(Frame{28}, 1.0f, EasingCurve{Easing::Linear});
    // + position_anim / scale_anim keyframes condizionali
});
```

## Semantic Loss Documentata (honesty closure)

La conversione fa un trade-off consapevole:
- Legacy `TextAnimMode::ByCharacter/ByWord/ByGlyph/ByLine` → **selettore per-glyph** + property per-unit (con stagger `delay_per_unit`)
- Conversione → **whole-layer keyframes** (no stagger per-unit)

Visivamente simile (fade-in/slide-in/scale-in layer-wise) ma NON byte-identical: le lettere non appaiono più una-per-una in "Build Faster" o "Think Different." ma come blocco unico.

Inlinato in ogni file di preset + bench_corpus un commento che forward-pointa al per-text Animator pipeline: `TextRunBuilder::append_animator(spec)` + selettori `TextSelectorUnit::Character/Word/Glyph` + `Animator` authoring façade con `.select(selector(Char).start(...).start(20, 1.0f)).opacity(0.0f).position(...).scale(...).release()` (già presente + verificata compilabile in `examples/getting_started/main.cpp:50` + `content/compositions/chronon_glow_final.cpp:174`).

## Critici di Accettazione (verificati)

- [x] **Build verde**: `cmake --build build/chronon/linux-fast-dev --target chronon3d_dev_fast -j8` exit 0 (post `rm` del .o stantio di `bench_corpus_scenes.cpp`).
- [x] **Smoke test runtime**: `chronon3d_cli list | grep BenchB02 + BenchB03` li elenca correttamente; `chronon3d_cli info BenchB0{2,3}_*` ritorna metadata corretti (1920x1080, 90 frame, 30/1 fps).
- [x] **Zero new SDK API symbols**: ZERO nuovi header, ZERO nuovi class/struct, ZERO nuove funzioni pubbliche. Tutto ri-uso di `LayerBuilder::animated_text()` + `LayerBuilder::opacity_anim/position_anim/scale_anim` + `text_preset()` + `AnimatedTextDocument::sample_at` (catena canonical esistente). Cat-3 anti-dup compliance verificato.
- [x] **Zero `#include <msdfgen>/<libtess2>/<unicode[/...]>`**: nessuna nuova include forbidden (Gate 5 Check 11 deny-everywhere preserved).
- [x] **Cat-2 freeze compliance**: ZERO nuovi simboli in `include/chronon3d/`.
- [x] **No duplication**: nessun nuovo shim `TextAnimator` legacy. La conversione inline-rewrite è il pattern raccomandato dal thinker-with-files-gemini (vs shim-adapter che avrebbe "duplicato l'API").

## Forward-points (per prossime sessioni)

1. **Per-text Animator pipeline (cat-3)** — ripristinare il per-glyph/per-word stagger semantic equivalente a `TextAnimMode::ByCharacter/ByWord/ByGlyph` migrando ogni call site convertito a `TextRunBuilder::append_animator(Animator("id").select(selector(unit).start(0,0).start(20,1)).opacity(0).position(...).scale(...).release())` (fase finale della conversione P1-10 dei presets).
2. **Bench-corpus test surface (cat-3)** — `examples/bench_corpus/bench_corpus_scenes.cpp` NON ha unit test. Aggiungere almeno un sanity test in `tests/bench_corpus/test_bench_corpus_scenes.cpp` che chiama B00..B11 `bench_bN*` factories e confronta dimension/duration/shape count dopo evaluate (lock-in regression).
3. **Stale `.o` rebuild hygiene (cat-4 ancillary gate)** — il `rm .o + rebuild` workaround manuale potrebbe diventare un periodic gate automatico. Vedi `tools/check_clean_rebuild.sh` (già esiste come opt-in `CHRONON3D_CLEAN_REBUILD=1`); valutare wiring in `wrap_push.sh` Step 4.5k.
4. **Migrate composizioni restanti in `content/` che usano ancora `TextAnimator` (audit forward-point)** — `rg '\bTextAnimator\b' content apps examples tests/install_consumer` (forward-point-search target).

## Cronologia

- 2026-07-14 — questo commit (`chore(build): unstick pre-existing rot build` candidates): 7 file modificati + 0 nuovi file (Cat-3 minimal-surface). Build verificato GREEN, smoke test eseguito su B02+B03.

## Cross-link

- AGENTS.md §`### Docs canonical update discipline rule` (Cat-5 3-doc atomic chaser pattern)
- AGENTS.md §Cat-3 anti-duplication discipline (no new SDK symbol, no new singleton/registry/resolver/cache, no new shim)
- AGENTS.md §Post-push SHA-selfcheck invariant (per `bash tools/wrap_push.sh origin main`)
- `TICKET-TEXT-LEGACY-POSITION-ROT` (sub-area ii closure, TextSpec::position → placement)
- `TICKET-TEXT-SPEC-FORWARDER-REMOVAL` (sibling LayerBuilder::text forwarder removal lineage)
- `TICKET-TEXT-DEFINITION-ADAPTER-SPLIT` (parent 4-phase text migration roadmap)
- `examples/getting_started/main.cpp:50` + `content/compositions/chronon_glow_final.cpp:174` (canonical `l.animated_text + Animator pipeline` pattern exemplar)
- `include/chronon3d/scene/builders/layer_builder.hpp:431` (canonical `animated_text(name, TextRunSpec)` declaration, returns `TextRunBuilder&`)
- `include/chronon3d/authoring/animator.hpp` (canonical `Animator` authoring façade: `.select().opacity().position().scale().tracking().release()` chain)
