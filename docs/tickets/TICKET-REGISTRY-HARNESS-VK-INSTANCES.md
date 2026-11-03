# TICKET-REGISTRY-HARNESS-VK-INSTANCES — il binario registry esaurisce le istanze Vulkan

## Stato

OPEN (2026-11-02). Eredità di
[`TICKET-POOL-SURFACE-REUSE`](archive/TICKET-POOL-SURFACE-REUSE.md) (chiuso) e
unico gate residuo di
[`TICKET-AE-EFFECT-PARITY`](archive/TICKET-AE-EFFECT-PARITY.md) per il
run completo `ctest`. Non è un difetto di rendering: **zero asserzioni
falliscono** in tutti i run.

## Sintomo

`chronon3d_backend_registry_tests` per intero, preset `linux-video-test` Debug,
host RTX A4000 / driver 595.99.02:

```
[doctest] test cases:     130 |      49 passed | 81 failed | 0 skipped
[doctest] assertions: 1906529 | 1906529 passed |  0 failed
EXIT=1
```

Tutti gli 81 "failed" sono la stessa eccezione, alzata da `check()` quando
`vkCreateInstance` restituisce `-9` (`VK_ERROR_INCOMPATIBLE_DRIVER`):

```
test_backend_registry_vulkan_dispatch.cpp:91: ERROR: test case THREW exception:
  Vulkan vkCreateInstance failed with VkResult=-9
```

Il primo caso che si interrompe è il primo di
`test_backend_registry_vulkan_dispatch.cpp`; da lì in poi **tutti** falliscono,
e il conteggio (81) è esattamente il numero di casi rimasti dopo che il processo
ha smesso di ottenere istanze.

## Cosa è stato escluso con una misura

1. **Non è un tetto del driver.** Un probe Vulkan puro (nessun codice Chronon3D)
   che crea e distrugge 200 volte istanza + device + coda riesce:
   `200 cycles OK (device=1)`, exit 0.
2. **`VK_ICD_FILENAMES` è dannoso e va evitato negli script.** Lo stesso probe
   con `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/nvidia_icd.json` fallisce a
   `create #26` con `-9`, mentre con `VK_DRIVER_FILES` (il nome corrente, già
   presente nell'ambiente di questo host) fa 200 cicli. La variabile deprecata
   trattiene la libreria ICD attraverso le istanze e satura il processo.
3. **Il file di parità non è più affetto.** Da quando
   `test_backend_registry_vulkan_effect_parity_test_support.hpp` usa una sola
   istanza per binario (`shared_vulkan_test_backend()`),
   `--test-case='Vulkan*'` dà **57 casi, 1.925.534 asserzioni, 0 fallite** in un
   solo processo, e `Strict Vulkan*` 25 casi e 5.422 asserzioni.

## Ipotesi ancora aperte

Le altre ~40 costruzioni di `VulkanBackend` che precedono il primo `-9`
avvengono in casi con backend su stack (`VulkanBackend backend;`), quindi il
distruttore gira. Restano da distinguere:

- una risorsa che `VulkanBackend::~VulkanBackend()` non rilascia (candidati: il
  descrittore/pipeline cache statico, le immagini con memoria esportabile, il
  messenger di debug);
- oppure un limite di *accounting* del driver che conta i device Vulkan creati
  nel processo e non le istanze, così che venti cicli di backend "puliti" lo
  esauriscano anche con teardown corretto.

## Prossimo passo (misura, non ipotesi)

Un probe che usi **solo** `VulkanBackend`: costruisci e distruggi N backend
(N = 60) e stampa a quale iterazione `vkCreateInstance` fallisce. Se fallisce a
~26–40 il difetto è nel teardown del backend (allora si biseziona: prima device,
poi immagini esportate, poi descriptor arena). Se non fallisce mai, il consumo
è dei test e la correzione è un'istanza per processo con reset di stato fra i
casi, non un fix del backend.

## Accettazione

- [ ] Il probe `VulkanBackend`-only dice se la risorsa trattenuta è del backend
      o dei test; il risultato è registrato qui con i numeri.
- [ ] `chronon3d_backend_registry_tests` completo gira **130/130 casi, 0
      interrotti** in un solo processo, con lo stesso numero di asserzioni
      verdi di oggi (1.906.529) o superiore.
- [ ] Nessun caso viene rimosso, skippato o riordinato per aggirare il limite,
      e nessuno script di misura esporta `VK_ICD_FILENAMES`.

## Riferimenti

- `tests/registry/test_backend_registry_vulkan_effect_parity_test_support.hpp`
  (`shared_vulkan_test_backend`).
- `src/backends/vulkan/vulkan_backend_lifecycle.cpp` (`VulkanBackend::~VulkanBackend`).
