# TICKET — Maintenance hotspots bonification (5 zone + 1 causa radice)

**Status:** `ACTIVE`
**Owner:** engine maintenance (C3D)
**Origine:** audit 2026-10-31 — churn analysis (`git log -80`, file touches), audit
completo [`fixes.md`](../../fixes.md) (checkout corrente), stato osservato
[`CURRENT_STATUS.md`](../CURRENT_STATUS.md).

**Principio guida del ticket stesso:** questo documento è umbrella e coordinamento
solamente. Le zone che hanno già un ticket owner restano lì (vedi W0 e W2); le
schede qui sotto dettagliano il piano operativo e puntano all'owner, non
duplicano lo scope.

---

## Premessa — la causa radice

Quasi tutti gli incident ricorrenti documentati in `fixes.md` sono **la stessa
classe di difetto**: *una seconda copia di un fatto che nessun gate vede*.

| Variante dello stesso difetto | Item `fixes.md` |
|---|---|
| 3 contenitori "parametri per-frame" con lo stesso nome, ownership non dichiarata | B.1 (differito) |
| Gate error-model che ricopia come literal la tassonomia che protegge (`min_matches = 28` = cardinalità dell'array in `error.hpp`) | B.2 (differito) |
| Codici errore camera derivati per substring dal testo dei messaggi | 3.2 |
| Due struct GPU omonime `GpuTextRunDynamic` a 64/128 byte; pipeline `TextBatch` registrata ma irraggiungibile → riattivazione = silent misread | G.1 (demolition aperta) |
| Commento shader 40 byte vs `static_assert` C++ 48 (guarda asimmetrica) | B.5 / 4.4 (✅ chiuso dal contract test) |
| Gate `steady_state_allocations` che certificava un run di un'altra revisione senza confrontare `source_revision` → falso PASS storico | B.4 (✅ revision binding applicato; F.2 policy del run aperta) |

**Conseguenza operativa:** ogni nuova feature / nuovo gate / nuova struct GPU può
ri-crearla. Le wave W1–W3/W5 eliminano le esemplificazioni attive; W4 elimina la
classe alla radice.

---

# W0 — CI same-SHA (owner: [`TICKET-MAIN-TRUTH-STAGE-1-2`](TICKET-MAIN-TRUTH-STAGE-1-2.md)) — COORDINAMENTO SOLAMENTE

**Perché è la zona #1:** ogni voce PARTIAL esiste perché la CI same-SHA è la
*condizione di sanità* del repo: senza di essa la certificazione resta manuale,
e la catena di gate può mentire in silenzio senza che qualcuno se ne accorga
(l'audit di `fixes.md` documenta due falsi PASS e il meccanismo come ripetibile).

**Azione qui:** nessuna — owner esclusivo del ticket collegato. Gate di uscita:
su `HEAD` corrente (`a214fad27...`) o successivo, `Chronon CI / Developer Gates /
CFI + Nightly + CodeQL + runtime/resource-leak/ABI/native-video` VERDI, same-SHA,
evidenza in `docs/evidence/`.

---

# W1 — GPU resource lifetime / teardown (Vulkan ↔ CUDA ↔ NVENC)

## Sintomi (fatti, non opinioni)

1. `tests/video/test_native_av_encoder_teardown_stress.cpp` → toccato **6 volte**
   negli ultimi 80 commit (file più toccato del repo).
2. La suite Vulkan debug **aborta ancora** in teardown con `free(): invalid pointer`
   (G.5 in `fixes.md`; nota 2026-09-25 e `CURRENT_STATUS.md` confermano open).
3. T05 (lights/shadows): **due `Lighting snapshots` a zero** con lifetime root
   cause aperta; i 3/3 backend test focusati verdi **non riproducono** il problema
   a scala graph.
4. Il churn conferma la zona: `vulkan_backend_impl.hpp`, `vulkan_backend_operations*_private.cpp`,
   `vulkan_surface_authority.hpp`, `native_av_encoder_{open,lifecycle,write,internal}.cpp`
   sono fra i file più toccati del periodo.

## Causa radice

- Ownership delle risorse GPU ripartita fra ≥6 authorities (`VulkanDescriptorAuthority`,
  `FrameSlotPool`, `GpuCompletionTracker`, `FrameQueue`, surface authority, encoder
  pools): invarianti singolarmente sensate, **ma nessun contratto di lifetime
  unitario** e **nessun riproduttore deterministico** a scala graph.
- I test focusati esercitano un componente alla volta: il bug vive nel *composito*
  (ordine di teardown, doppio release, snapshot fuori lifetime) e il test
  component-wise non lo vede.

## Piano (3 passi, in ordine)

### W1.1 — Riproduttore base (prima di ogni refactoring)
Partire da **`test_vulkan_debug_context`** (case in cui l'abort è deterministico) e:
- (a) minimizzare il caso che abortisce (fino al minimo numero di nodes/pipelines
  che risveglia il `free() invalid`);
- (b) identificare il **contratto di lifetime violato**: quale handle viene
  rilasciato due volte o usato oltre la vita della sua authority.
Gate W1.1: un test riproducibile con errore bit-deterministico. Nessun fix prima
del riproduttore.

### W1.2 — Contratto di lifetime unico
- Documentare in `docs/architecture/RUNTIME-OWNERSHIP-MATRIX.md`, per ogni family
  (surface, descriptor set, snapshot, encoder frame, atlas page): *chi crea*,
  *chi invalida*, *quando*.
- **Strumento concreto:** estendere il pattern generazionale già canonico con un
  `GenerationalId` (generation + authority id) su surfaces, descriptor sets,
  snapshots, encoder frames. Un `release` su generation mismatch = **fail-fast
  con log**, non silente.
- Nessuna nuova interop in `src/backends/vulkan/*` senza il contratto (gate arch).

### W1.3 — Teardown stress come gate, non come bug chased
- Estendere il suite teardown-stress a **tutti gli ordini** di teardown:
  normale, encoder mid-flight, partial graph failure, exit-before-prepare.
- Regola arch: il suite resta **same-SHA verde in CI**; ogni nuovo pool/authority
  introduce un caso in questo suite.
Gate W1.3: `ctest -R teardown_stress` verde; **un solo framework di test
graph-lifetime** (requisito esplicito: il caso del riproduttore W1.1 deve
*smettere* di passare se il contratto viene tolto — otherwise è un test che non
protegge nulla).

## Exit condition W1
`free() invalid` riprodotto → risolto ∧ suite stress verde ∧ root cause di T05
("Lighting snapshots a zero") spiegata col contratto di lifetime (fix alla
causa, non workaround).

## NON fare
- Non ottimizzare performance in questa wave (i lock hot path sono W3).
- Non introdurre una **seconda** authority per "risolvere" il lifetime.

---

# W2 — Doppia pipeline di esecuzione (compat vs tipata) — owner: [`TICKET-WAVE-CLOSEOUT-DEMOLITION-DEBT`](TICKET-WAVE-CLOSEOUT-DEMOLITION-DEBT.md) — COORDINAMENTO SOLAMENTE

Definizione e scope sul ticket owner (C20 lowering tipizzata → C22 corpus → C23
demolition). Qui soltanto: **perché è in questa lista** e schedule operativa.

**Perché è in questa lista:** è il *moltiplicatore* di manutenzione strutturale —
ogni nuova capa / ogni nuovo effetto va mantenuto **due volte** (o drift). Il path
compat contiene inoltre throw per condizioni attese (3.1, stati di scheduling che
nella maggioranza dei casi non sono errori), eviction FIFO e 3-thread pipeline che
per design cherode essere ottimizzati (C24 — già correttamente congelato).

**Schedule operativo (esecuzione; definizione sul ticket owner):**
1. C22 caso 1 (transform animata) come caso minimo che esercita **l'intera
   infrastruttura di confronto** (due percorsi, stessi input, hash frame-by-frame).
   Verde quello, gli altri 7 casi sono variazioni di contenuto, non di
   infrastruttura.
2. Non parallelizzare con W1/W3: esecuzione in serie, non in parallelo.
3. A C22 verde su 8/8 casi: C23 demolition (scope già bloccato nel ticket owner).
   Effetto: migliaia di righe in meno e l'eliminazione del "perché serve cambiare
   in due luoghi" più grande rimasto nell'engine.

---

# W3 — Text hot path (`GpuGlyphAtlas`, struct GPU testo, contratti layout)

Tutte le evidenze sono nell'audit `fixes.md`; qui il piano esecutivo.

## W3.1 — `GpuGlyphAtlas`: generation counter (item 2.2)
- **Stato:** `location()` ×2 + `metrics()` + `page_handle()` = **4 acquisizioni
  `shared_lock(m_mutex)` per glyph per frame** sul mutex più caldo del draw path.
- **Fix:** validare la prepared run con **1 atomic load** (`generation`) dell'atlas;
  le location sono già snapshot in `GlyphStatic`; ogni invalidate dell'atlas
  incrementa generation, e una prepared run stale viene ri-preparata. Nessun lock
  per glyph.
- Passi:
  1. `std::atomic<uint64_t> m_generation` nell'atlas; bump a ogni
     invalidate/push pagina;
  2. `GpuPreparedTextRun` memorizza generation alla creazione; `execute`
     ri-prepara se `prepared.generation != atlas.generation` → esito via
     `Result`/optional (niente throw, coerente con 3.1);
  3. rimuovere i 4 lock dal draw lane; `location()`/`metrics()` tengono lock
     solo per lookup freddi (registrazione/evizione).
- Gate: suite text verdi (prepared-MTSDF unica rotta nativa graph); counter
  `m_lock_wait_us` dell'atlas ∼0 sul corpus statico corrente (evidence in
  `docs/evidence/`).
- Rischio: basso. **Prima fix di W3 da eseguire** (miglior rapporto payoff/rischio).

## W3.2 — Demolizione `TextBatch` (item G.1)
Pipeline creata e registrata all'avvio
(`vulkan_backend_lifecycle_private.cpp`), shader compilato ed embedded,
**zero call-site di produzione**; riattivandola il record 64-byte verrebbe
letto come 128-byte = silent corruption senza errore di build.
- Checklist demolition (già enumerata in G.1): `record_text_batch`
  (definizione + dichiarazione in `vulkan_backend_impl.hpp:425`),
  `GpuKernelId::TextBatch`, registrazione/pipeline nel lifecycle,
  `text_batch.comp`, entry CMake, embed SPIR-V, test ABI relativo.
- Precondizione: ri-verify zero refs a livello repo, documentata nel commit.
- Gate: build verde, suite text/ABI verdi, e seguendo lo stesso schema
  anti-resurrection già usato per la demolizione del pipe FFMPEG (per evitare la
  resurrezione).

## W3.3 — Layout contract generalizzato (residuo di G.5)
- `tests/test_shader_layout_contract.py` copre `text_run.comp`; estenderlo alle
  coppie `GpuGlyphStatic`/`GlyphStatic` e `GpuTextRunDynamic`/`TextRunDynamic`
  dei due `text_tile_*` (oggi verificate a mano via `glslangValidator`, non
  asserite nel test).
- Passo strutturale successivo (la sua PR): `tools/shaders/spirv_abi_gen.cpp`
  emette **member offset + size** delle storage buffer structs, così il test
  consuma l'ABI generata e le copie manuali vengono eliminate.
Gate: `test_shader_layout_contract.py` rileva mutazioni delle nuove struct
(commento stale, membro aggiunto, membri scambiati, retype) — la stessa
auto-prova già sviluppata per `text_run.comp`.

## NON fare
- Non toccare shaping/building MTSDF/tessellazione in questa wave (fuori scope).
- Non attribuire le fix in blocco: ciascuna separata, con il suo gate.

---

# W4 — Macchina di certificazione veritiera (decisioni differite B.1/B.2 + B.4-F.2 + 3.2)

- **B.1 (differito in F.2):** decidere l'asse dei 3 contenitori per-frame
  (`compiler::FrameParameterBlock` / `graph::FrameParameterBlock` /
  `graph::FrameParameterTable`). È una decisione di ownership, non un rename:
  - Owner A = whole-frame per-layer sampled (vivo);
  - Owner B = per-binding transform (solo `PrecompNode`);
  - Owner C = per-frame-time slice (`CompiledFrameGraph`/`GPUCommandPlan`/
    `CompiledComposition`).
  Proposta da valutare: rinominare B `PerBindingTransformBlock`, mantenere A come
  canonical per i sampled layer params, C per la table per-frame; documento mappa
  in `docs/architecture/` + eliminazione degli owner decaduti dopo il rename.
- **B.2 (differito):** 18 regole `*_error_construction_adapter_only` con
  allowlist hardcodata → **1 regola data-driven** (registro nel TOML o
  annotazione `CHRONON_ERROR_ADAPTER` nel codice, letta da `check_architecture.py`).
  Elimina la change amplification ×3 per ogni nuovo tipo errore.
- **B.4-F.2:** policy su dove vive il report del run corrente per
  `steady_state_allocations` (proposta: sidecar del run con hash del binary,
  in `docs/evidence/runs/<sha>/`; la baseline resta solo riferimento).
- **3.2:** eliminare `derive_code_from_message` (`shot_timeline.cpp:335-355`):
  propagare il codice strutturato dall'origine (`Result<T,E>` già canonico).

Gate W4: selftest del gate esteso (case non-dati + mutazioni); il passcount attuale
(184/184 storico) non è la metrica — la metrica è: **nessun gate che contiene
literal copiati dall'engine** (il gate *legge* la conoscenza che protegge, non la
ricopia).

---

# W5 — Lane effetti / per-pixel (disciplina, non demolizione)

- **Stato:** registry + corpus parity esistono e sono verdi lato storico (48/48
  native rows alla SHA archived; catalogo corrente: 57 effetti, 51 native,
  6 fail-closed espliciti). Ma il churn dice che ogni effetto nuovo finisce per
  toccare file hot path (`per_pixel_dof_node.cpp`, `effect_stack_node*`).
- **Regola da trasformare in gate arch:** un effetto nuovo esiste **solo**
  (a) come riga nel `EffectCatalog` + parametri tipizzati + caso nel corpus
  parity; **mai** come nuovo node ad-hoc variante (l'eccezione `PerPixelDofNode`
  resta il precedente chiuso dal consolidation WAVE-DOF).
- **Azione:** regola in `tools/architecture_rules.toml` (nuovo kind: file nuovo
  in `src/render_graph/nodes/` non presente nel registry del catalogo →
  `GATE_FAIL`, con allowlist esplicita delle eccezioni documentate). Collocare
  anche la promozione stroke-solid candidates di `fill_rect.comp` (nota
  2026-09-25) sotto la **stessa** regola: corpus parity prima che diventi route.
- Ogni nuovo effetto AE registra il suo caso in
  [`TICKET-AE-EFFECT-PARITY.md`](TICKET-AE-EFFECT-PARITY.md).

---

## Ordine di esecuzione (in serie; solo le dipendenze qui indicate)

```text
 1. TICKET-MAIN-TRUTH-STAGE-1-2 (CI same-SHA verde)   ← priorità assoluta
 2. W1.1 riproduttore teardown (bit-deterministico)   ← sblocca W1
 3. W3.1 generation counter atlas                     ← alto payoff / basso rischio
 4. W3.2 demolizione TextBatch                        ← elimina silent-corruption latente
 5. W1.2 contratto lifetime → W1.3 stress gate        ← il grosso di W1
 6. W3.3 layout contract generalizzato
 7. W4 (B.1, B.2, B.4-F.2, 3.2)
 8. W2 C22 (dopo che W1 non tocca le stesse file)
 9. W5 gate effetti
10. W2 C23 demolition (a C22 verde)
```

**Regola trasversale (ogni PR di questo ticket):** documentazione aggiornata nella
stessa PR; una sola fixture per volta; nessun PASS presentato senza same-SHA;
evidence sotto `docs/evidence/`.

## Definition of Done (ticket)

- [ ] CI same-SHA verde (owner: MAIN-TRUTH)
- [ ] W1: riproduttore risolto via contratto; teardown-stress come gate; root cause T05 chiusa
- [ ] W3.1 / W3.2 / W3.3 applicati con gate
- [ ] W4: item applicati o decisi; nessun gate con literal-duplicati
- [ ] W5: regola effetti attiva come gate arch
- [ ] W2: C22 corpus verde 8/8; C23 demolition landed (owner ticket)
- [ ] Indice aggiornato: riga in `FOLLOWUP_TICKETS.md` + una riga di sintesi in `CURRENT_STATUS.md`
