# TICKET-V2-VALUEMAP-PROPS-MERGE

**Status:** DONE (2026-07-14, chore commit)
**Area:** Timeline / Composition descriptor / Props routing
**Audit cross-link:** §1 (descriptor scaffolding), §2 (CLI/JSON props pipeline), §8 (CR-2 props有效性), §21-23 (legacy factory cleanup)

---

## Problema

`TypedCompositionDescriptor<Props>` esponeva già `defaults / validate /
resolve_metadata / factory`, ma la pipeline `ValueMap (CLI/JSON) →
Props tipizzate → validate → factory` era interrotta al primo step.
L'implementazione conteneva un placeholder esplicito:

```cpp
// TICKET-TO-DO — ValueMap → Props merge:
// ValueMap is the CLI/C-ABI bridge (see AGENTS.md § ValueMap).
// Currently CLI/JSON overrides in cprops.values are ignored;
// only typed_defaults reach the factory.  Merge logic (JSON
// schema or structured-binding walk) is deferred to a follow-up
// ticket — the descriptor scaffolding is the priority for B2.
```

Conseguenza: una composizione registrata via
`TypedCompositionDescriptor<NewsIntroProps>{}.to_descriptor()`
riceveva dal registry solo i `typed_defaults`; ogni override da
`--props-file custom.json` o `--prop title="..."` veniva scartato
al confine `CompositionProps::values` prima di raggiungere la
factory tipizzata. Le `CompositionRegistry::create(name, cprops)`
con `cprops.values` popolate non producevano alcuna differenza di
output.

Il audit §23 segnalava inoltre che — una volta implementato il
merge — ogni parsing manuale di `ValueMap` dentro le factory dovesse
essere rimosso. Una grep sistematica (comando `rg`) per i pattern
`parse_from`, `props_map.`, `composition_props.values.`, `p.values.`,
`require_string | require_float | require_int | get_string | ...`
restituisce **0 match** nei sorgenti: le factory attuali non parsano
`ValueMap` direttamente; il vero blocker per il cleanup è il TODO
sopra. Risolvere il merge chiude indirettamente il §23.

## Soluzione

Estendere `TypedCompositionDescriptor<Props>` con un quinto callback
`decode`:

```cpp
template <typename Props>
struct TypedCompositionDescriptor {
    std::string id;
    std::string category;
    Props defaults{};

    std::function<std::optional<std::string>(const Props&)> validate;

    std::function<CompositionMetadata(const Props&)> resolve_metadata;

    // ── new field ────────────────────────────────────────
    std::function<chronon3d::Result<Props, PropsError>(
        const ValueMap&, const Props&)> decode;
    // ─────────────────────────────────────────────────────

    std::function<Composition(const Props&)> factory;
};
```

`to_descriptor()` invariato in shape, ma la wrap-factory ora
esegue in sequenza:

1. `Props props = typed_defaults` (bootstrap dai default tipizzati).
2. Se `decode` è presente: `auto decoded = typed_decode(cprops.values, typed_defaults);`
   - errore → `throw std::runtime_error("Composition '" + comp_id + "' decode failed: [" + err.key + "] " + err.message);`
   - successo → `props = std::move(decoded).value();`
3. Se `validate` è presente: chiama validate sulla `props` MERGED.
4. `return typed_factory(props)`.

`resolve_metadata` resta invariata: continua a valutare su
`typed_defaults` (pre-decode) così il registry può rispondere
`list`/`info` con `width/height/fps/duration` **prima** di invocare
la factory, senza richiedere un round-trip di decode esterno.

`PropsError` (definita in `composition_props.hpp` accanto a
`ValueMap`) è una struct POD ergonomica:

```cpp
enum class PropsErrorReason {
    MissingRequired,   // key assente nella ValueMap
    BadType,           // valore presente ma non parsabile
    OutOfRange,        // valore parsato ma fuori dominio
    InvalidFormat      // asset path / color / altri structured format
};

struct PropsError {
    std::string      key;      // offending ValueMap key (vuoto se generale)
    PropsErrorReason reason;
    std::string      message;  // human-readable detail
};
```

Niente reflection/runtime secondo quanto prescritto dal audit §2:
`audit §1+§2 vietano esplicitamente reflection C++ generica, array
annidati, oggetti arbitrari nella prima fase`. Il `decode` callback
è una lambda user-supplied che chiama direttamente le accessor
di `ValueMap` (`get_string`, `require_int`, `contains`, …) — type-safe
e triviale da ragionare.

## Vincoli rispettati

1. **No reflection runtime.** L'uso di `tl::expected`/`std::expected`
   non era adottabile (richiederebbe C++23); si riusa il
   `chronon3d::Result<T, E>` canonico (incl. factory implicite,
   `value()`, `error()`, `has_value()`) definito in
   `include/chronon3d/core/types/result.hpp`. AGENTS.md v0.1
   `## Regole di lavoro` "No nuovi singleton/registry/resolver/cache
   senza ADR" → riusato `Result` esistente, niente nuovo
   `PropsResult<T>` ad-hoc.
2. **No nuovi ABI-break.** Aggiungere un campo a
   `TypedCompositionDescriptor<Props>` non rompe l'ABI perché la
   struct è template-typed-header-only (nessuna istanza
   precompilata). `CompositionDescriptor` (la forma non-templated
   usata dal registry) **non** è stata modificata → il registry
   storage `std::function<Composition(const CompositionProps&)>`
   resta bit-identico.
3. **Forward-compat preservata.** `TypedCompositionDescriptor<Props>`
   adopters senza `.decode` (call sites pre-ticket) continuano a
   funzionare: la wrap-factory controlla `if (typed_decode)` prima
   di invocarlo; senza decode, si cade direttamente al passo
   `props = typed_defaults`. Questo chiude il rischio di regressione
   su authoring scaffolding che importa il descriptor pre-ticket.
4. **No espansione inutile di API.** Aggiunti 1 tipo (`PropsError` +
   enum) + 1 campo (`decode`); nessun nuovo registro, resolver,
   schema, reflection tipo.

## Test cases aggiunti

`tests/timeline/test_composition_descriptor_decode.cpp` (NUOVO,
registrato in `tests/timeline_tests.cmake`):

| # | Test                                                   | Contratto chiuso |
| - | ------------------------------------------------------ | ---------------- |
| 1 | no-decode → factory receives defaults                  | forward-compat storica preservata |
| 2 | decode + matching keys → merged                        | pipeline canonica funziona |
| 3 | decode MissingRequired → factory throws                | error path con key+message |
| 4 | decode BadType → factory throws                        | error path con key+message |
| 5 | validate runs AFTER decode on merged Props            | validate vede gli override CLI |
| 6 | registry.create round-trip + descriptor metadata       | end-to-end via registry |

Il test 6 verifica anche che `CompositionDescriptor::{width, height,
fps, duration}` sono popolati dai `typed_defaults` (non dal valore
CLI) → il registry può rispondere list/info senza round-trip di
decode.

## File modificati / aggiunti

| File                                                                | Tipo     |
| ------------------------------------------------------------------- | -------- |
| `include/chronon3d/timeline/composition_props.hpp`                 | MOD      |
| `include/chronon3d/timeline/composition_descriptor.hpp`            | MOD      |
| `tests/timeline/test_composition_descriptor_decode.cpp`             | NEW      |
| `tests/timeline_tests.cmake`                                        | MOD      |
| `docs/tickets/TICKET-V2-VALUEMAP-PROPS-MERGE.md` (questo)          | NEW      |

## Discipline AGENTS.md rispettate

- **No aggiornamento canonicali.** `CURRENT_STATUS.md`,
  `FOLLOWUP_TICKETS.md`, `CHANGELOG.md`, `ROADMAP.md` rimangono
  intatti per questo chore (vedi tabella "Disciplina di
  aggiornamento dei canonici"). La presente scheda ticket è la
  cronaca home Cat-3 anti-dup.
- **Ticket-home only per cronaca estesa.** Per i piccoli chore, la
  regola è "tutta la narrativa nel ticket". Nessun paragrafo esteso
  nei canonicals.
- **One chore = one commit on main.** Push via `tools/wrap_push.sh
  origin main` + SHA-triple self-check post-push (regola Post-push
  SHA-selfcheck invariant in AGENTS.md v0.1 §`## Regole di lint
  documentale`).
- **No new singleton/registry/resolver/cache senza ADR.** `Result`
  riusato. `PropsError` aggiunto accanto a `ValueMap` per coesione.

## Forward-point (non blocker, cronaca qui)

- **TICKET-V2-VALUEMAP-PROPS-MIGRATION-EXT** *(planned futuro)*:
  Adottare `TypedCompositionDescriptor<SpecT>` con `.decode`
  esplicito nelle composizioni ora registrate via legacy
  `registry.add("Name", factory)` con `const CompositionProps&`
  ignorato. Questa adozione è la sostanza del ticket E1 del
  piano (audit §21) — rimandato a sessione successiva in quanto
  scope > 100 lines, doccia di composizioni da migrare, e
  richiede ADR-026 (forward-point già documentato in
  `docs/ROADMAP.md`).
- **TICKET-V2-VALUEMAP-PROPS-DECODE-EXAMPLES** *(planned futuro)*:
  Aggiungere 1-2 composition canary pubbliche (es. `NewsIntro`,
  `ProductLaunch`) che esibiscono l'API `decode` agli utenti
  come pre-canned example. Da coordinare con i ticket A2/A3/A4
  del piano per non duplicare il wiring CLI (`--props-file`).
- **TICKET-COMPOSITIONREGISTRY-ID-PRESERVE** *(pre-existing bug,
  surfaced by this chore's TC6)*: `CompositionRegistry::add()`
  esegue `descriptors_[std::move(descriptor.id)] = std::move(descriptor)`;
  il primo `std::move(descriptor.id)` svuota `descriptor.id` per usarlo
  come chiave del map; il secondo `std::move(descriptor)` sposta il
  descrittore (con `id` già azzerato) nel map value. Risultato: il
  descrittore nel map ha sempre `id == ""`. Il lookup
  `descriptor_of(name)` ritorna correttamente il descrittore associato
  alla chiave, ma `descriptor_of(name)->id` ritorna stringa vuota.
  Bug pre-esistente (NON introdotto dal chore E2). Fix proposto:
  `descriptors_[descriptor.id] = std::move(descriptor);` (NO std::move
  sulla id per la chiave). Out-of-scope per E2 per Cat-3 anti-dup +
  "Fare PR piccole e mirate". Tracciato qui come forward-point.
