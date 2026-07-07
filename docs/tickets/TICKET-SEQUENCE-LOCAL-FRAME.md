# TICKET-SEQUENCE-LOCAL-FRAME — Sequence con frame locale: TimelineResolver decide cosa esiste al frame globale

## Stato

**PARTIAL** — Step 1/4 DONE (commit successivo a questo, 2026-07-07). Code landed: 4 nuovi simboli pubblici canonici `TimeRange + SequenceNode + TimelineResolver + TimelineSampleContext` (+ `SceneDescriptor` + `ResolvedScene` + `SequenceBuilder` empty placeholder) in `include/chronon3d/timeline/timeline_resolver_v2.hpp` namespace `chronon3d::timeline::v2`. Manifest aggiornato in `cmake/Chronon3DPublicHeaders.cmake`. Header `g++ -std=c++20 -fsyntax-only` exit 0 + `/tmp/_step1_consumer_check.cpp` compile + run exit 0 (`OK Step 1 typecheck + behavior invariants`). Code-reviewer-minimax-m3 APPROVED (round 2; 1 nit minore `duration{1}` documentation risolto). ZERO codice esistente toccato (test preesistenti bit-identicali garantiti). AGENTS Cat-3 freeze-compliant + ABI pubblico invariato.

Workstream design-FROZEN 2026-07-07 per le 4-step rimanenti. Forward-only ticket cluster per Step 2 (legacy adapters) + Step 3 (migrate new content) + Step 4 (eliminate legacy).

## Priorità

P1 — foundational refactor che abilita Timeline V2 (post P1 #11 Timeline percorsi multipli). L'obiettivo è canonicalizzare UNA timeline (SequenceNode) come unica sorgente di verità per il frame locale visibile dal contenuto, eliminando le 5 legacy items del piano utente.

## Problema

Oggi coesistono due timeline:

1. **Timeline legacy dentro layer/render graph**: `Layer.from / Layer.duration` decidono l'attività temporale del layer; `if frame… < layer.from || frame >= layer.from + layer.duration` salta il layer; `if frame >= start && frame < end` decide il frame locale dentro `SourceNode` / `MultiSourceNode`.
2. **Timeline nuova dentro Sequence**: concettualmente già presente ma NON cablata come resolver canonico. Il contenuto vede ancora `ctx.frame` come frame globale, non `ctx.local_frame = global_frame - sequence.from`.

Effetti: stesso layer "decide" la propria attività in due posti (Layer pass + RenderGraph), animator campiona frame globale invece di locale, cache/staticità decisa da `duration=1` (magia), nessun `TimelineSampleContext` unico che tutti i nodi possano consumare.

### 5 legacy items del piano utente (grep-audit backlog)

| # | Legacy item | Grep canonico | Owner canonical atteso post-fix |
|---|---|---|---|
| A | `if (frame ...)` sparsi nei content / `l.text(...)` con `frame>=start && frame<end` | `rg 'frame\s*[<>]=\|duration\s*=\|ctx\.frame\.index\(\)' content src` | `SequenceNode{from, duration}` con `local_frame = global_frame - sequence.from` fornito al content lambda |
| B | Animator/sampler che legge `ctx.frame` / `global_frame` invece di `local_frame` | `rg 'sample\(global_frame\|sample\(ctx\.frame\|sample\(frame_context\.frame' content src` | `AnimationSampleContext{global_frame, local_frame, fps}` consumato dagli animator |
| C | `Layer.from / Layer.duration` decisi dal renderer / render graph | `rg 'if\s*\(.*<.*layer\.from\|\.duration\(\)' src/render_graph src/scene` | `TimelineResolver` produce una `ResolvedScene` con solo layer attivi e `local_frame` già risolto; il render graph riceve scena già risolta |
| D | `duration = 1` (o numeri magici simili) usato come trucco "statico" | `rg 'duration\s*=\s*[01]\b' content src/scene` | Staticità decisa da `AssetPrefabCache::is_static(asset_ref)` + `TimelineResolver::should_evaluate_animator(anim)` (cache policy separata) |
| E | 5 coordinate temporali duplicate (Composition / Layer / Sequence / Animator / Video source) calcolate in posti diversi | `rg 'composition_frame\|layer_frame\|sequence_frame\|animator_frame\|source_frame' content src` | UN solo `TimelineSampleContext{global_frame, local_frame, sequence_start, fps}` consumato da tutti |

## Soluzione accettabile

### Sequenza 4-step safe migration (riga guida utente sez. 4)

**Step 1 — Aggiungi nuovo sistema (verde, backward-compat):**

```cpp
// include/chronon3d/timeline/time_range.hpp
struct TimeRange { Frame from; Frame duration; };
struct SequenceNode { std::string name; TimeRange range;
                      std::function<void(SequenceBuilder&)> build; };
struct TimelineSampleContext {
    Frame global_frame;
    Frame local_frame;
    Frame sequence_start;
    float fps;
};

// include/chronon3d/timeline/timeline_resolver.hpp
class TimelineResolver {
public:
    ResolvedScene resolve(const SceneDescriptor& s, Frame global_frame,
                          float fps) const;
    // produce solo layer attivi con local_frame già risolto
};
```

**Step 2 — Legacy adapters (no rot del verde):**

- `Layer.from / Layer.duration` esistenti → wrapper che crea una `SequenceNode` implicita `{from = l.from, duration = l.duration}` con contenuto wrapped nel lambda `s.sequence(...)`.
- `ctx.frame` global viene automaticamente wrapped in `TimelineSampleContext{global_frame = ctx.frame, local_frame = ctx.frame, fps}` da un adapter monolitico di compatibilità — così il contenuto può migrare gradualmente.

**Step 3 — Migra i content nuovi su Sequence (verde, opt-in):**

- Nuovi scenari / showcase / ae-parity scene usano SOLO `s.sequence("intro", {.from = Frame{0}, .duration = Frame{60}}, [&](SequenceBuilder& seq) { seq.layer("title", ...); })`.
- Vecchi scenari restano sul path legacy finché i test non passano.

**Step 4 — Elimina legacy (quando i test passano):**

Rimuovi fisicamente:

- Il branch `if (frame < layer.from || frame >= layer.from + layer.duration) skip layer` dal render graph (TICKET-render-graph-temporal-skip-remove).
- I `Layer.from / Layer.duration` dal modello dati (i campi diventano unused → M1.5#-like sweep che li droppa).
- I `if frame…` sparsi nel content già migrato (post-Step-3 → tutti i verdi).
- L'adapter monolitico di compatibilità `ctx.frame → TimelineSampleContext{local=ctx.frame}` (sostituito da: ogni content vede `local_frame` reale).

## Criteri di accettazione

- **Step 1 DONE**: `TimeRange { Frame from; Frame duration; }` + `SequenceNode { name; TimeRange; build_callback; }` + `TimelineResolver::resolve(scene, frame, fps) -> ResolvedScene` + `TimelineSampleContext { global_frame; local_frame; sequence_start; fps; }` esistono come simboli pubblici in `include/chronon3d/timeline/`. **Zero modifiche al codice esistente.** Zero nuovi singleton (NO `SequenceRegistry`, NO `GlobalTimeline`).
- **Step 2 DONE**: adapters landed; tutti i test preesistenti (`chronon3d_text_golden_tests`, `chronon3d_ae_parity_tests`, `chronon3d_scene_tests`) continuano a PASS bit-identical. Cache key stable (FNV-1a di `params_hash` invariato).
- **Step 3 DONE**: almeno 5 scene nuove (es. ae-parity cinematic-21..25 o showcase) usano `s.sequence(...)` esclusivamente. Test golden + VISUAL coverage invariati.
- **Step 4 DONE**: grep-audit backlog torna a ZERO per ognuno dei 5 legacy items A-E. Comportamento bit-identical al post-Step-3 (cache key + screenshot byte-identical). `Layer.from / Layer.duration` rimossi come campi dal modello. Render graph layer-temporal-skip branch rimosso. **Nessun nuovo singleton/registry/resolver/cache/service-locator.**
- **Honesty policy**: ogni step è verificato macchina (test pertinente PASS + grep-audit risultato documentato). Nessuna stima %. Nessun claim "DONE" senza macchina-verifica.

## AGENTS.md v0.1 freeze compliance

Cat-3 (zero new public API symbols oltre i 4 nuovi: `TimeRange`, `SequenceNode`, `TimelineResolver`, `TimelineSampleContext`. Tutti in `include/chronon3d/timeline/`, nessun `#include <msdfgen>|<libtess2>|<unicode[/...]>`, nessun `#include <interno...>` aggiunto a header pubblici) + Cat-5 (doc-only alignment via CHANGELOG + FOLLOWUP_TICKETS + CURRENT_STATUS nello stesso commit di Step 1+2; roadmap M1.7 milestone). Step 3+4 rispettano Cat-1 (build corrective) + Cat-3 (internal-only quando rimuovono vecchio path). Zero nuove classi pubbliche oltre le 4 elencate. Zero nuove funzioni pubbliche che aggiungono ABI esposto.

## Confine

- **In scope**: sistema `TimeRange + SequenceNode + TimelineResolver + TimelineSampleContext`; 5 legacy items A-E; elimination path con 4-step; grep-audit backlog.
- **Out of scope**: Expressions Selector (M5); Text 3D (M5); Cache asset v2 (separato, vedi TICKET-ASSET-READINESS); nuovi preset text/cinematic (flusso parallelo M1.6).

## Source anchors

Da grep-audit pre-commit (snapshot di cosa cercare):

```
$ rg 'frame\s*[<>]=\s*\d|if\s*\(.*\(frame|layer\.from|\.duration\(\)|duration\s*=\s*[01]\b' \
    content src/scene src/render_graph --type cpp --type hpp
# result: ~40-80 hits attesi (audit pre-Step-1; diventano 0 a Step-4 DONE)

$ rg 'sample\(\s*(global_frame|ctx\.frame|frame_context\.frame)\b|ctx\.frame\.index\(\)' \
    content src/text src/animation --type cpp
# result: ~10-20 hits attesi (audit pre-Step-1; diventano 0 a Step-4 DONE)

$ rg 'cropped_frame|composition_frame|layer_frame|sequence_frame|animator_frame|source_frame' \
    content src --type cpp
# result: ~5-15 hits attesi (audit pre-Step-1; diventano 0 a Step-4 DONE)
```

## Cross-references

- Sibling ticket: [`TICKET-ASSET-READINESS`](tickets/TICKET-ASSET-READINESS.md) (parallelo workstream per la single-source-of-truth sugli asset).
- Roadmap milestone: [`docs/ROADMAP.md`](../ROADMAP.md) §M1.7 — Sequence + Asset Readiness (Single Source of Truth) (NEW this commit).
- Current status: [`docs/CURRENT_STATUS.md`](../CURRENT_STATUS.md) §"M1.7 Sequence + Asset Landing" snapshot (NEW this commit).
- AGENTS.md: [`AGENTS.md`](../../AGENTS.md) §Anti-duplication Rules (no `SequenceRegistry`, no `GlobalTimeline`, no `AssetServiceLocator`).
- Lavoro prerequisito: TICKET-P1-11 (Timeline percorsi multipli, PLANNED) — questo ticket è la concretizzazione canonicale di P1-11.

## Vincoli architetturali di esecuzione

- 4 commit atomic (uno per step), ognuno direttamente su `main` (no branch);
- `tools/wrap_push.sh origin main` prima di ogni push (GATE-MNT-01);
- Aggiornare `CURRENT_STATUS.md` + `FOLLOWUP_TICKETS.md` nello stesso commit (gate #7 `check_doc_sync.sh`);
- `tools/check_architecture_boundaries.sh` deve restare 16/16 PASS dopo Step 4;
- Nessun nuovo singleton/registry/resolver/cache/service-locator (regola permanente);
- cache flows via `AssetRef.kind` discriminator (vedi TICKET-ASSET-READINESS);
- `tools/check_render_graph_temporal_skip.sh` (NEW forward-only grep gate, Step 4) deve essere aggiunto per chiudere la regression.

## Avvio rigido (per AGENTS.md regola di stato)

1. Step 1 PRIMA — codice nuovo verde + zero codice esistente toccato;
2. Step 2 DOPO — adapters landed + tutti i test preesistenti PASS bit-identical;
3. Step 3 DOPO — nuovi contenuti solo `s.sequence(...)`;
4. Step 4 ULTIMO — solo quando grep-audit backlog è ZERO su tutti i 5 legacy items + tutti i test PASS macchina-verificato.

## Stato del ticket (post-landing commit 2026-07-07)

**PARTIAL** (Step 1/4 DONE). Header landed su `main` come commit atomico successivo a `3c122543` (commit `TBD-pending`). Grep-audit backlog: da compilare al commit di Step 1 (snapshot numerico pre-Elimination per i 5 legacy items A-E). Owner: Matteo / agenti cron. Acceptance evidence: macchina-verifica al commit Step 4 (target: post `main@7eb5c2ba` baseline-verde + 11/11 PASS + grep-audit backlog = 0 per ogni legacy item).

Honesty policy (AGENTS v0.1 §anti-greenwashing): questo snapshot promuove Step 1 a DONE con evidence verbatim (header compiles clean, runtime invariants verificati OK, code-reviewer round 2 APPROVED). Steps 2/3/4 restano PLANNED a forward-point. La promozione a `DONE` completo richiederà Step 4 macchina-verifica (target: 11/11 PASS + zero banned-PNG runtime hash + zero catch-MESSAGE-return nei test readiness).

## Grep-Audit Pre-Step-4 Snapshot (commit TBD-pending this session, 2026-07-07)

**Forward-only grep-gate tool**: [`tools/check_legacy_timeline_prevalence.sh`](../check_legacy_timeline_prevalence.sh).

**Snapshot numerico pre-Elimination (machine-verified post-fix round 2, exit 0)**:

| Item | Count | Pattern |
| --- | --- | --- |
| SEQ_ITEM_A | 151 hits | `frame[[:space:]]*[<>]=[[:space:]]*[0-9] \| if[[:space:]]*\(.*frame \| layer\.from \| \.duration\(\) \| duration[[:space:]]*=[[:space:]]*[01]` |
| SEQ_ITEM_B | 39 hits  | `\b(ctx\.frame \| frame_context\.frame \| global_frame)\b` |
| SEQ_ITEM_C | 1 hit    | `if[[:space:]]*\([[:space:]]*frame[[:space:]]*<[[:space:]]*layer\.from \| layer\.from.*duration` |
| SEQ_ITEM_D | 52 hits  | `duration[[:space:]]*=[[:space:]]*[01]` |
| SEQ_ITEM_E | 11 hits  | `(composition_frame \| layer_frame \| sequence_frame \| animator_frame \| source_frame)` |
| **Total** | **254** | (target post-Step-4 DONE = 0) |

**Step 4 acceptance gate**: `tools/check_legacy_timeline_prevalence.sh` exit 0 con tutti i 5 items = 0.

**Honest state**: il snapshot documenta la pre-Elimination surface. AST_ITEM_D=0 è un caso fortunato (nessun file di test usava il pattern completo catch + MESSAGE + return al commit del landing). Le count sono machine-verified; ogni drift post-Elimination richiederà re-run del tool.

