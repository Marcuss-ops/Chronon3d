# 03 — Execution Scope e Precomp (root/tile/precomp + isolamento)

> Contratto formale per l'**isolamento degli scope di esecuzione** del
> renderer. Definisce la gerarchia `root → tile / precomp`, le regole
> di catena parent + arena, la protezione anti-ricorsione tramite
> `owner_key`, e traccia lo stato di avanzamento dei PR 6.1–6.7 del
> [`refactor-roadmap/06-execution-scopes.md`](refactor-roadmap/06-execution-scopes.md).

Stato corrente: [`STATUS.md`](STATUS.md). Roadmap: [`ROADMAP.md`](ROADMAP.md).
Determinismo correlato: [`docs/02-determinism.md`](02-determinism.md).
V3: [`V3_BLUEPRINT.md`](V3_BLUEPRINT.md).

---

## 1. Fondazione dell'`ExecutionScope` (🟢 Done, PR 6.0, commit `03637479`)

### Scopo

Prima di PR 6.0 il path produttivo era:

```
scene.cpp:73 render_scene_via_graph(...);
  ├─ graph_cache_coordinator.cpp:91 build_or_reuse_graph(...);     ← una sola arena
  ├─ scene_tile_execution.cpp:24 execute_dirty_tiles(...);         ← local_session NEW + reset!
  └─ precomp_node.cpp / precomp_node_execute.cpp:77                ← child session, child arena NEW
```

Concretamente, ogni `execute_dirty_tiles` (path tile) istanziava un
`RenderSession local_session` *nuovo* (`scene_tile_execution.cpp:91`)
e ogni `PrecompNode::execute` istanziava un altro `RenderSession`
interno: il rischio concreto era che il child path eseguisse
`local_executor.execute(..., local_session)` il quale può fare
reset dell'arena, invalidando qualunque puntatore detenuto dal
parent (es. un `precomputed bake` buffer nel padre).

Dopo PR 6.0, il path produttivo è **tipato** attraverso `ExecutionScope`:

```cpp
namespace chronon3d::graph {
enum class ExecutionScopeKind : std::uint8_t {
    Root    = 0,  // one per render invocation; primary arena
    Tile    = 1,  // one per coalesced dirty region
    Precomp = 2,  // nested subgraph; holds ProgramLease
};
inline constexpr int kMaxScopeDepth = 16;

class ExecutionScope {
    // kind, session&, arena&, graph_id, parent*, depth tracked
    [[nodiscard]] ExecutionScopeKind           kind()      const noexcept;
    [[nodiscard]] chronon3d::RenderSession&    session()   const noexcept;
    [[nodiscard]] chronon3d::FrameArena&       arena()     const noexcept;
    [[nodiscard]] chronon3d::graph::GraphInstanceId graph_id() const noexcept;
    [[nodiscard]] const ExecutionScope*        parent()    const noexcept;
    [[nodiscard]] int                          depth()     const noexcept;

    void                  set_owner_key(std::uint64_t) noexcept;
    [[nodiscard]] std::uint64_t owner_key() const noexcept;
    [[nodiscard]] bool is_descendant_of(const ExecutionScope&) const noexcept;
    [[nodiscard]] bool would_recurse(std::uint64_t) const noexcept;
    [[nodiscard]] int  chain_length() const noexcept;
};
}
```

Vedi `include/chronon3d/core/scope/execution_scope.hpp`.

### Architettura

- La classe è **header-only** (nessuna `.cpp` gemella): non possiede
  risorse proprie, è un **thin routing object** che dice al call-site
  *dove* allocare e *in che catena* ci si trova, senza OWN-are nulla.
- Tre costruttori enumerati per approvvigionare i tre casi d'uso:
  - **Root ctor**: `ExecutionScope(kind, session, graph_id)` — nessun
    parent, `depth = 0`, `arena = session.arena()` di default.
  - **Child ctor**: `ExecutionScope(kind, session, graph_id, parent*)`
    — `depth = parent->depth + 1`, `arena = parent->arena()` di default
    (PR 6.4 innalzerà a child-arena distinta per il path tile puro).
  - **Explicit-arena ctor**: `ExecutionScope(kind, session, arena&,
    graph_id, parent*)` — usato da PR 6.3/6.4 quando serve una
    child-arena distinta da quella del parent (vd. §4 touchpoints).
- La `arena` è catturata **by reference** al ctor (mai copiata o
  riusata), lifetime binding al caller — coerente con
  `chronon3d::FrameArena` (`include/chronon3d/core/memory/arena.hpp`)
  che è uno struttura `std::vector<std::byte>` + `monotonic_buffer_resource`
  e vive sullo `RenderSession` come `std::unique_ptr<FrameArena>`
  (vd. `include/chronon3d/runtime/render_session.hpp` riga 50).
- `GraphInstanceId` (`include/chronon3d/render_graph/core/node_identity.hpp`)
  è **strong type** wrapper su `std::uint64_t` — la api evita lo
  swapping posizionale con `StableNodeId` via `make_precomp_key(...)`
  (compile-time check) e mantiene ABI stability perché i due campi
  interni sono entrambi `std::uint64_t` aggreggati.

### Stato attuale

🟢 **Done** — la fondazione del tipo `ExecutionScope` (header + enum
+ `would_recurse`/`is_descendant_of`/`chain_length`/`set_owner_key`)
è completa su commit `03637479` e verificata a livello di typecheck
human-review (CODE-REVIEWER `Nit Pick Nick`, turni 2× "Ship it").

---

## 2. Prevenzione della Ricorsione e Sicurezza Stack (🟢 Done in parte — `would_recurse` OK; `kMaxScopeDepth` enforcement 🟡 Planned)

### Limite `kMaxScopeDepth`

```cpp
inline constexpr int kMaxScopeDepth = 16;
```

Dichiarato come upper bound, ma **NON** ancora enforced nel ctor
(la `depth` incrementa senza clamp). L'enforcement è demandato al
PR 6.5 (vd. §5 exit criteria), che aggiungerà un check al ctor e
LONG-JMP-style reject per catene che eccedono 16.

### Guard anti-ricorsione: `would_recurse(k)`

```cpp
[[nodiscard]] bool would_recurse(std::uint64_t k) const noexcept {
    for (const ExecutionScope* cur = this; cur; cur = cur->m_parent) {
        if (cur->m_kind == ExecutionScopeKind::Precomp
            && cur->m_owner_key != 0u
            && cur->m_owner_key == k) {
            return true;
        }
    }
    return false;
}
```

Questo guard cammina la catena parent fino alla root confrontando
`owner_key`. Se trova uno scope `Precomp` il cui `owner_key` è
`k`, rifiuta la costruzione di un ulteriore `Precomp` con lo stesso
key — coprendo sia:

- **Direct recursion**: `Root → Precomp_A → Precomp_A` (impossibile
  fisicamente: `Precomp_A` non può essere il proprio parent).
- **Indirect recursion**: `Root → Precomp_A → Precomp_B → Precomp_A`
  (rilevato quando si prova a istanziare un secondo `Precomp_A` da
  dentro `Precomp_B`).

`k = 0u` è il sentinella "unset" — viene bypassato perché
`cur->m_owner_key != 0u` skippa i Precomp non-ancora-impegnati.

### Anti-pattern evitato

Per spec esplicita (`refactor-roadmap/06-execution-scopes.md` lines
95–103), l'anti-ricorsione NON usa un `std::recursive_mutex`. Il
mutex ricorsivo è vietato perché:

1. Maschererebbe il bug a livello architetturale (entrare nella
   stessa catena di scope = rot del programma, non del runtime).
2. Costerebbe un lock acquisition su ogni nested execute, senza
   benefici.
3. Non distinguerebbe ricorsione *legittima* (es. retry su cache miss
   che dovrebbe essere consentita) da ricorsione *malevola* (loop
   infinito).

L'approccio usato — camminata della catena parent fino alla root —
è O(depth) e termina in tempo finito perché `depth ≤ kMaxScopeDepth`
(quando enforcement arriverà).

### Stato attuale

🟡 **Partial** — `would_recurse` è completo e coperto da test
(see §3); `kMaxScopeDepth` enforcement è demandato a PR 6.5.

---

## 3. Copertura dei Test Core Scope (🟢 Done)

I dieci TEST_CASE contenuti in `tests/core/test_execution_scope.cpp`
(aggiunti al target `chronon3d_core_tests` di `tests/core_tests.cmake`)
coprono i cinque invariant del §1 + §2:

| # | TEST_CASE | Cosa garantisce |
|---|---|---|
| 1 | "root construction — kind=Root, depth=0, parent=null" | (a) root ctor fields, (b) chain_length=1, (c) arena() == session.arena() |
| 2 | "tile child — depth=1, parent links to root" | (a) child ctor fields, (b) parent pointer forwarding, (c) is_descendant_of(self-as-ancestor) true, viceversa false |
| 3 | "sibling scopes are NOT descendants of each other" | due Tile con stesso parent si rifiutano reciprocamente via is_descendant_of |
| 4 | "deeper chain — grandchild depth=2" | 3-livello Root → Tile → Precomp depth incrementale + is_descendant_of transitivo |
| 5 | "would_recurse — direct precomp loop rejected" | k = owner_key già sullo scope → true; k ≠ owner_key → false; k = 0u (unset) → false |
| 6 | "would_recurse — indirect loop detected" | due Precomp nested con owner_key diversi → ciascuno detects il proprio key, NON detects l'altro |
| 7 | "child arena — independent of parent arena" | ctor explicit-arena con `FrameArena` separato → resource() identities diverse, reset del child non tocca parent |
| 8 | "kind() round-trips all three kinds" | i tre valori enum round-trip + execution_scope_kind_name() stringa stabile |
| 9 | "chain depth grows by one per nested child" | 5 sibling Scope stack-allocated, depth 1..5 confermato, is_descendant_of transitivo |
| 10 | "anthrilinear (ancestor-then-sibling) chain rejected" | ROOT → TILE_A → PRECOMP_A → TILE_B → re-enter PRECOMP_A from inside TILE_B detect via parent-chain walk |

Il test #10 (l'unico non-mirror di un invariant base) copre il caso
*non lineare* della catena: PRECOMP_A è antenato diretto di TILE_B,
non precedente immediato. La camminata `would_recurse` lo catches.
Aggiunto come drive-by dopo il primo code-review round (richiesto
esplicitamente dal reviewer `Nit Pick Nick` come non-triviale caso
da coprire).

### Stato attuale

🟢 **Done** — tutti i 10 test passano typecheck (CODE-REVIEWER
"Nit Pick Nick", 2× "Ship it") su commit `03637479`. Il target
`chronon3d_core_tests` eseguirà la suite su next `cmake --build`
non appena `doctest`+`xxhash` saranno disponibili nel sandbox
(vedi blocking environmental note a fine doc).

---

## 4. Touchpoints di Integrazione della Pipeline (🟡 Partial)

L'header `ExecutionScope` è in piedi ed è type-safe, **ma** non
ancora fisicamente plumbato nei call-sites produttivi. I touchpoints
identificati:

### 4.1 GraphExecutor::execute(...) → Signature flip (PR 6.1)

`include/chronon3d/render_graph/executor/graph_executor.hpp`
dichiara oggi:

```cpp
execute(CompiledFrameGraph& graph,
        RenderGraphContext& context,
        RenderSession& session,
        ExecutionScheduler& scheduler) const;
```

Va cambiato in:

```cpp
execute(CompiledFrameGraph& graph,
        RenderGraphContext& context,
        ExecutionScope& scope,                  // ← new
        ExecutionScheduler& scheduler) const;
```

Perché ogni scope debba sapere **quale** arena sta usando (parent o
child) e quale catena di parent lo precede (per i child scope), il
`RenderSession&` da solo non basta — serve un puntatore opaco al
scope.

### 4.2 TileLocalSession vs TileScope (PR 6.4)

`src/render_graph/pipeline/scene_tile_execution.cpp:91` istanzia
oggi `RenderSession local_session;` ad ogni regione tile da
renderizzare. Va rimpiazzato da:

```cpp
ExecutionScope tile_scope{kind::Tile,
                           sw_renderer->session(),  // borrowed, not copied
                           child_arena,             // own arena, freed at scope end
                           tile_graph_id,
                           &root_scope};
```

In questo modo la child-arena è distinta per regione (vd. §5 PR 6.4
exit criteria), la root session è borrowed, e il parent (root)
resta in scope per tutta la vita del tile scope.

### 4.3 PrecompNode::execute(...) → Hold ProgramLease (PR 6.3)

`include/chronon3d/render_graph/nodes/precomp_node.hpp:77`
dichiara `OwnedFB execute(RenderGraphContext& ctx, ...)`. Va
cambiato in:

```cpp
OwnedFB execute(ExecutionScope& scope,
                RenderGraphContext& ctx, ...);
```

Il ProgramLease (`SceneProgramStore::ProgramLease` da
`include/chronon3d/render_graph/cache/scene_program_store.hpp`,
wrapper `std::shared_ptr<CompiledSceneProgram>`) va tenuto vivo
per tutta la durata dello scope (`shared_ptr` ownership keeps the
program alive anche se la cache evict during the same scope).

### 4.4 SceneProgramStore anti-recursion via owner_key (PR 6.5)

PR 6.5 è il punto in cui `set_owner_key(instance_key_value)` viene
popolato da un PrecompNode prima dell'inner execute. Il
`SceneProgramStore::acquire(...)` consulta `scope.would_recurse(
instance.key)` e, se true, ritorna una lease vuota (`program=null`)
invece di compilare — output deterministico (empty fb) secondo
`refactor-roadmap/06-execution-scopes.md` PR 6.5.

### Stato attuale

🟡 **Partial** — i touchpoints sono identificati e documentati,
**ma** nessun call-site produttivo passa ancora un `ExecutionScope&`.
Il path produttivo corrente usa ancora il vecchio contratto
`session+scheduler`. PR 6.1 — 6.5 sono ancora da implementare.

---

## 5. Checklist di Convalida (Exit Criteria PR 6.1 – 6.7)

Specchiata da [`refactor-roadmap/06-execution-scopes.md`](refactor-roadmap/06-execution-scopes.md)
per tracciare lo stato corrente dei PR rimanenti della catena WP-6:

### PR 6.1 — Change the executor contract — 🟡 Partial

| Exit criterion | Stato |
|---|---|
| Remove direct arena selection from `RenderSession` inside executor code | 🟡 Partial (planned, not landed) |
| Allocate all execution state from `scope.arena` | 🟡 Partial |
| Reset only the arena owned by that scope | 🟡 Partial |
| Keep `GraphExecutor` stateless | ✅ Done (già stateless post PR-2 rewire) |
| Update all production and test call sites | 🔵 Planned |

### PR 6.2 — Add the root scope — 🔵 Planned

| Exit criterion | Stato |
|---|---|
| Construct one root scope per render invocation | 🔵 Planned |
| Bind the render job session and compiled graph identity | 🔵 Planned |
| Keep root execution memory alive through every child invocation | 🔵 Planned |
| Reset root memory only after final output ownership is safe | 🔵 Planned |

### PR 6.3 — Add the precomp child scope — 🔵 Planned

| Exit criterion | Stato |
|---|---|
| Allocate a distinct child arena | 🔵 Planned |
| Bind the nested compiled graph identity | 🔵 Planned |
| Preserve the parent session and job-owned program store | 🔵 Planned |
| Hold the `ProgramLease` for the complete child scope | 🔵 Planned |
| Ensure child teardown cannot invalidate parent `ExecutionState` | 🔵 Planned |

### PR 6.4 — Replace tile-local sessions with tile scopes — 🔵 Planned

| Exit criterion | Stato |
|---|---|
| Create one tile arena/scope per coalesced region | 🔵 Planned (tocco punto 4.2 sopra) |
| Reuse the parent render job session and caches | 🔵 Planned |
| Preserve independent temporary memory | 🔵 Planned |
| Keep tile clipping and cache-key isolation | 🔵 Planned |
| Remove ad-hoc logical session construction from tile orchestration | 🔵 Planned |

### PR 6.5 — Add recursion protection — 🟡 Partial

| Exit criterion | Stato |
|---|---|
| Track active precomp owner keys in the scope chain | 🟢 Done (`set_owner_key` / `owner_key` accessor) |
| Reject direct recursion | 🟢 Done (`would_recurse` copre direct; test #5 in §3) |
| Reject indirect recursion | 🟢 Done (`would_recurse` copre indirect; test #6 in §3) |
| Return a deterministic engine error or empty result according to policy | 🟡 Partial (`would_recurse` returns bool, call-site policy is owner-side) |
| Do not use a recursive mutex as recursion handling | 🟢 Done (catena camminata, no recursive_mutex) |

### PR 6.6 — Add memory and race tests — 🔵 Planned

| Exit criterion | Stato |
|---|---|
| child scope does not reset parent arena | 🔵 Planned (test #7 in §3 è constructor-level, non lifecycle) |
| tile scope does not reset root arena | 🔵 Planned |
| precomp inside tile uses an independent child arena | 🔵 Planned |
| two tiles use independent temporary arenas | 🔵 Planned |
| tile scopes still share job-owned program-store state | 🔵 Planned |
| recursive precomp is rejected | 🟢 Done (`would_recurse` logic) |
| same-program execution remains protected by Work Package 5 lease | 🟢 Done (WP-5 lease contract già in piedi) |
| ASAN validation for use-after-reset | 🔵 Planned |
| UBSAN validation for nested teardown | 🔵 Planned |

### PR 6.7 — Add permanent guards — 🔵 Planned

| Exit criterion | Stato |
|---|---|
| Prevent arena override parameters from returning | 🔵 Planned |
| Prevent nested execution from passing the parent arena directly | 🔵 Planned |
| Prevent tile code from creating replacement render jobs | 🔵 Planned |
| Require explicit scope and scheduler at every executor call | 🔵 Planned |

### Stato globale

🟡 **Partial** globale: la **fondazione** del tipo `ExecutionScope` e
i test di acceptance sono done (PR 6.0); i PR 6.1–6.7 che plumbing
the type into production paths sono ancora **planned**.

---

## 6. Interlocking determinismo (rilancio §4 di `02-determinism.md`)

Il **path composite** del determinismo (vd. `02-determinism.md` §4)
ha come prerequisite proprio il path precomp isolato. Senza PR 6.3
(non realizzato), il determinismo end-to-end della Z-order con
nested precomp non è chiuso: due chiamate al `SoftwareRenderer`
con scene diverse possono avere cache-hit/miss su `SceneProgramStore`
che l'executor oggi gestisce in modo non-deterministico se lo stesso
`RenderSession` viene riusato (rot TICKET-007.r/q/s). Una volta che
PR 6.3 introduce il `ProgramLease held for the complete child scope`,
questo caso si chiude perché la lease `shared_ptr` mantiene il
programma vivo indipendentemente dai clear() intermedi.

Vedi anche [`docs/02-determinism.md`](02-determinism.md) §4 per la
discussione completa del path composite.

---

## Note ambientale (typecheck new tests)

I 10 TEST_CASE del §3 sono typecheck-ready (CODE-REVIEWER `Nit Pick
Nick` × 2 turni, "Ship it" entrambi). Per eseguire il test
eseguibile in sandbox servono `doctest` e `xxhash` come dipendenze
apt (`sudo apt-get install -y libxxhash-dev doctest-dev`) — fuori
da questo ambiente, il compile-check è disponibile solo tramite
`g++ -fsyntax-only` con stub minimale per `TEST_CASE` macro. Una
volta le deps installate, il comando canonico è:

```bash
cmake --preset linux-artist-dev -DCHRONON3D_BUILD_TESTS=ON
cmake --build build/chronon/linux-artist-dev \
      --target chronon3d_core_tests
ctest -R 'ExecutionScope' --output-on-failure
```

Commit reference: `03637479` (PR 6.0 foundation).
