# TICKET-POOL-SURFACE-REUSE — un render che riusa il device legge pixel del render precedente

## Stato

**CLOSED (2026-11-02).** Diagnosi iniziale **sbagliata**: il difetto non è il
riuso di una superficie del pool. La causa reale è aritmetica, nello shader:
`edge_index()` in `src/backends/vulkan/shaders/color_matrix.comp` mappava le
coordinate di campionamento con il `%` intero, il cui lowering su questo driver
è errato per operando sinistro negativo, e l'Offset porta la coordinata sotto
zero al bordo sinistro/superiore. Corretto alla radice e coperto da un test di
regressione che esegue la matrice su **un solo** device.

## Riquadro storico: cosa si era ipotizzato e perché era sbagliato

La scheda nasceva dall'osservazione che l'Offset, eseguito con un device
condiviso fra campioni, sbagliava dal sesto in poi (`0.52` dove la sorgente vale
`0.26`), mentre con un device per campione passava tutto. Da lì l'ipotesi "la
superficie presa dal pool conserva i pixel del render precedente".

Due fix candidati sono stati implementati, misurati e **respinti** perché non
cambiavano il risultato:

1. **Invalidare `initialized` alla release** (`release_surface_now` →
   `VulkanSurfaceAuthority::invalidate_released_slot`): 36 asserzioni ancora
   fallite. La guardia `slot_in_use()` è vera in quel punto, quindi la superficie
   non veniva invalidata affatto.
2. **Invalidare `initialized` sul bind** (`materialize_binding`, quando
   `surface_bindings` non contiene ancora l'handle): 36 asserzioni ancora
   fallite, stessi texel e stessi valori.

Entrambi sono stati rimossi dal codice: nessun fix parziale è rimasto in giro.

Il dato che ha demolito l'ipotesi: **il dump è identico con un device nuovo e
con un device riusato**. Se il difetto fosse contenuto stantio, un device nuovo
non potrebbe riprodurlo. Il riuso del device era una coincidenza: la matrice
falliva perché i campioni `Wrap`/`Mirror` erano gli ultimi della sequenza, non
perché il device fosse sporco.

## Causa reale

```glsl
// prima
if (mode == 2) return ((i % size) + size) % size;      // Wrap
int p = ((i % period) + period) % period;              // Mirror
```

Con `Offset` e offset `x = 1.25`, il tap bilineare al bordo sinistro calcola
`floor(coord.x - 1.25 - 0.5)`, che al primo texel vale `-2` e al secondo `-1`.
Su questa GPU (RTX A4000, driver 595.99.02) `-1 % 14` restituisce **3**, non
`-1`: il compilatore abbassa il modulo intero a una divisione per reciproco e
il caso negativo esce sbagliato. Il campione legge quindi la colonna 3 al posto
della 13, che è esattamente il `0.52` visto al posto di `0.26`.

I primi cinque campioni passavano perché non mappano mai un indice negativo
attraverso `%`: `Transparent` esce prima (`coord < 0` → `vec4(0)`) e `Clamp`
clampa. Solo `Wrap` e `Mirror` arrivano al modulo con operando negativo.

### Fix

`edge_index()` usa il modulo floor-based definito dallo spec
(`x - y * floor(x / y)`) invece del `%` intero:

```glsl
if (mode == 2 && size > 0) return int(mod(float(i), float(size)));
if (mode == 3) { int period = 2 * size - 2; int p = int(mod(float(i), float(period))); ... }
```

Le coordinate restano dentro il range intero esatto dei float a 24 bit
(`|offset| ≤ 1e6` per contratto), quindi la conversione non introduce
approssimazione sui casi di produzione.

## Evidenza (host RTX A4000, driver 595.99.02, preset `linux-video-test` Debug)

- Test di regressione
  `Vulkan Offset samples negative coordinates on a single reused device`
  (`tests/registry/test_backend_registry_vulkan_effect_parity.cpp`): matrice
  4 edge mode × 2 filtri × 4 offset (inclusi `{1.25, -0.75}`, `{-1.25, 0.75}`,
  `{-9.5, 5.25}`) su **un solo** `VulkanBackend` e **un solo**
  `EffectScratchResources` → **1 caso, 64/64 asserzioni PASS** (32 campioni,
  ognuno con `REQUIRE(ok)` + `CHECK(mismatches == 0)`), exit 0.
- Matrice wave-1 completa sullo stesso device condiviso: caso
  `Vulkan native color effects match the software processors` (tutte le 16
  subcase, `Offset` compresa) → **PASS**, 138 asserzioni con
  `Vulkan native color effects*` (2 casi).
- Nessuna regressione: `--test-case='Vulkan*'` → **57 casi, 1.925.534
  asserzioni, 0 fallite, exit 0** in un solo processo; `--test-case='Strict
  Vulkan*'` → 25 casi, 5.422 asserzioni, 0 fallite. La selezione `Vulkan*`
  include ora i casi di parità che la scheda aveva visto interrompersi, e li
  esegue con una sola istanza Vulkan.

Il device condiviso ora è il default dell'harness di parità
(`shared_vulkan_test_backend()` in
`test_backend_registry_vulkan_effect_parity_test_support.hpp`): una sola
istanza Vulkan per binario, leakata di proposito perché distruggere un device
da un distruttore statico trasforma un run verde in un crash all'uscita.

## Perché contava oltre i test

Il difetto era reale in produzione: qualunque Offset/effetto con
`Wrap`/`Mirror` e un campione che va sotto zero (bordo sinistro o superiore)
leggeva il texel sbagliato. Non dipendeva dal pool, quindi non serviva alcun
handshake di superfici: la superficie era corretta, era la coordinata a essere
calcolata male.

## Accettazione

- [x] Test di regressione che esegue la matrice `Offset` su **un solo** device e
      pretende la parità con il software oracle su tutti i campioni, incluso
      `Wrap` + `Bilinear` (32 campioni, 64 asserzioni).
- [x] Ripetere la matrice wave-1 con un solo device e restare verde: PASS, ed è
      ora il default dell'harness di parità.
- [x] Nessuna regressione sulla matrice esistente: `Vulkan*` 57/57 casi e
      1.925.534 asserzioni verdi; `Strict Vulkan*` 25/25 e 5.422.

## Eredità aperta (non di questa scheda)

Il limite `vkCreateInstance -9` che aveva motivato la scheda resta un problema
**dell'harness**, non del renderer, ed è ora tracciato separatamente:
[`TICKET-REGISTRY-HARNESS-VK-INSTANCES`](../TICKET-REGISTRY-HARNESS-VK-INSTANCES.md).
Un probe Vulkan puro fa 200 cicli create/destroy di istanza+device senza errori,
quindi il tetto non è del driver: è il binario di test che trattiene risorse
quando crea un backend per caso.

## Riferimenti

- [`TICKET-AE-EFFECT-PARITY`](TICKET-AE-EFFECT-PARITY.md) (archiviato),
  checkpoint "Copertura ColorMap e fix dell'harness — 2026-11-02" e
  "Chiusura implementativa — 2026-11-02 (seconda sessione)".
- `src/backends/vulkan/shaders/color_matrix.comp`, `edge_index()`.
