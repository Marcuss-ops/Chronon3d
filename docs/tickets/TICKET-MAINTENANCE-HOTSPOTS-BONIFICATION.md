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

## W3.1 — `GpuGlyphAtlas`: generation counter (item 2.2) — LAND 2026-11-11
- **Stato:** il batch lock è già in produzione: `resolve_resident()` proietta
  location + page + metrics sotto **un solo `shared_lock` per run**, quindi la
  prepare/draw path non paga più 4 acquisizioni per glyph. Restava il generation
  counter.
- **Fix (LAND):** `GpuGlyphAtlas::m_generation` atomico + `generation()`;
  `GpuPreparedTextRun::atlas_generation` catturato alla prepare; il draw path
  ricostruisce la run quando l'epoch non coincide. `clear()` e `attach()`
  avanzano l'epoch; la crescita delle pagine **non** la avanza, perché non
  invalida location o handle esistenti. Nessun lock per glyph.
- Passi:
  1. `std::atomic<uint64_t> m_generation` nell'atlas; bump su
     invalidate/reattach (non su page growth);
  2. `GpuPreparedTextRun` memorizza generation alla creazione; `execute`
     ri-prepara se `prepared.generation != atlas.generation` → esito via
     `Result`/optional (niente throw, coerente con 3.1);
  3. rimuovere i 4 lock dal draw lane; `location()`/`metrics()` tengono lock
     solo per lookup freddi (registrazione/evizione).
- Gate: suite text verdi (prepared-MTSDF unica rotta nativa graph); counter
  `m_lock_wait_us` dell'atlas ∼0 sul corpus statico corrente (evidence in
  `docs/evidence/`).
- Rischio: basso. **Prima fix di W3 da eseguire** (miglior rapporto payoff/rischio).

## W3.2 — Demolizione `TextBatch` (item G.1) — CHIUSO 2026-11-11
Verifica sul tree corrente: `text_batch.comp` non esiste, `record_text_batch`
non ha definizione né call-site, `GpuKernelId::TextBatch` non esiste, la
lifecycle non registra la pipeline e `src/backends/vulkan/CMakeLists.txt` non
compila lo shader. `GpuPassKind` conserva il valore 10 come riservato
(`gpu_command_plan.hpp`), quindi la demolizione non può essere riattivata per
errore. Restano solo riferimenti storici in `fixes.md` e nei ticket; nessun
codice di produzione.
- Gate: build Release `-j12` verde e suite text/ABI verdi sul tree che contiene
  questa verifica.

## W3.3 — Layout contract generalizzato (residuo di G.5) — LAND 2026-11-11
- **Stato:** `tests/test_shader_layout_contract.py` copre `text_run.comp`,
  `GpuTextRunDynamic`/`TextRunDynamic` e ora anche `GpuGlyphStatic`/`GlyphStatic`
  sui due `text_tile_*`: sequenza dei word packati a 32 bit, size std430 e
  `ArrayStride` SPIR-V compilato. Il test passa con `glslangValidator` presente.
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

- **B.1 — LAND 2026-11-11:** asse deciso e applicato. Owner A resta
  `compiler::FrameParameterBlock` (whole-frame sampled); owner B è stato
  rinominato `graph::PerBindingTransformBlock`; owner C resta
  `graph::FrameParameterTable` (per-frame-time slice). La mappa a tre assi è in
  `docs/architecture/RUNTIME-OWNERSHIP-MATRIX.md`; `PrecompNode` usa solo
  `m_transform_block` e il vecchio header è stato rimosso. Test precomp 11/11.
- **B.2 — LAND 2026-11-11:** le 20 guardie `*_construction_adapter_only`
  hardcodate sono state sostituite da `[error_adapter_inventory.adapters]` nel
  TOML. `check_architecture.py` genera gli scan dal registro, valida ogni
  `legacy_type` contro `kLegacyErrorMappings` e fallisce se compare uno scan
  adapter hardcoded. Selftest esteso a 32/32; gate arch 218/218.
- **B.4-F.2:** policy su dove vive il report del run corrente per
  `steady_state_allocations` (proposta: sidecar del run con hash del binary,
  in `docs/evidence/runs/<sha>/`; la baseline resta solo riferimento).
- **3.2 — CHIUSO/RITIRATO 2026-11-11:** `derive_code_from_message` non esiste
  più nel tree; il path `camera_v1/shot_timeline.cpp` è stato ritirato. Restano
  solo riferimenti storici in `fixes.md`; nessun adapter o fallback nuovo.

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
  [`TICKET-AE-EFFECT-PARITY.md`](archive/TICKET-AE-EFFECT-PARITY.md).

---

## Ordine di esecuzione (in serie; solo le dipendenze qui indicate)

```text
 1. TICKET-MAIN-TRUTH-STAGE-1-2 (CI same-SHA verde)   ← priorità assoluta
 2. W1.1 riproduttore teardown (bit-deterministico)   ← sblocca W1
 3. W3.1 generation counter atlas — LAND 2026-11-11
 4. W3.2 demolizione TextBatch — CHIUSO 2026-11-11 (già assente dal tree)
 5. W1.2 contratto lifetime → W1.3 stress gate        ← il grosso di W1
 6. W3.3 layout contract generalizzato — LAND 2026-11-11
 7. W4 — B.1/B.2/3.2 LAND 2026-11-11; B.4-F.2 decision remains
 8. W2 C22 — CHIUSO 2026-11-10 (compat chain deleted; corpus NOT RUN)
 9. W5 gate effetti
10. W2 C23 — CHIUSO 2026-11-10 (executor removed; no PASS claimed)
```

**Regola trasversale (ogni PR di questo ticket):** documentazione aggiornata nella
stessa PR; una sola fixture per volta; nessun PASS presentato senza same-SHA;
evidence sotto `docs/evidence/`.

## Definition of Done (ticket)

- [ ] CI same-SHA verde (owner: MAIN-TRUTH)
- [ ] W1: riproduttore risolto via contratto; teardown-stress come gate; root cause T05 chiusa
- [x] W3.1 / W3.2 / W3.3 applicati con gate (2026-11-11: build Release `-j12`, suite cache 206/206, registry atlas PASS, contract test PASS)
- [x] W4: B.1/B.2/3.2 LAND 2026-11-11; 20 adapter data-driven, selftest 32/32, arch 218/218; B.4-F.2 policy remains a separate decision
- [ ] W5: regola effetti attiva come gate arch
- [x] W2: C22/C23 compatibility chain removed 2026-11-10; corpus NOT RUN (legacy lane deleted), no PASS claimed
- [ ] Indice aggiornato: riga in `FOLLOWUP_TICKETS.md` + una riga di sintesi in `CURRENT_STATUS.md`
