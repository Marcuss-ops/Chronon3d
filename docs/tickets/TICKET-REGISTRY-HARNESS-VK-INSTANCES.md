# TICKET-REGISTRY-HARNESS-VK-INSTANCES — il binario registry esaurisce le istanze Vulkan

## Stato

PARTIAL (2026-11-11). Eredità di
[`TICKET-POOL-SURFACE-REUSE`](archive/TICKET-POOL-SURFACE-REUSE.md) (chiuso) e
unico gate residuo di
[`TICKET-AE-EFFECT-PARITY`](archive/TICKET-AE-EFFECT-PARITY.md) per il
run completo `ctest`. Il `-9` storico è un difetto dell'ICD NVIDIA headless;
con llvmpipe il `-9` è assente, ma restano **9 mismatch reali di effect
parity** (non un problema del harness).

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

## Misura 2026-11-03 (questo host, sessione headless)

Il probe `VulkanBackend`-only non è servito: il limite si raggiunge anche col
binario completo e un preload su `vkCreateInstance` lo rende visibile senza
toccare i test.

- 26 creazioni riuscite; la 27ª restituisce `-9` e da lì in poi **tutte**
  falliscono (81 chiamate).
- Tra la 26ª e la 27ª il processo chiude 25 fd del driver NVIDIA
  (`/dev/nvidiactl`, `/dev/nvidia0`, `/dev/dri/renderD128`, `nvidia-modeset`,
  GLCache, `sync_file`) e smappa ~190 MB. Alla call 1 e alla 26: 29 fd, 403
  mappe, ~875 MB di VmSize; alla 27: 4 fd, 151 mappe, 683 MB. Nessuna crescita
  per istanza: non è un esaurimento di fd, thread o memoria.
- `--test-case='TrackMatteNode*,SourceNode*,Vulkan*'`: 60/60 casi,
  1.925.642 asserzioni, 0 `-9` in un processo fresco.
- `--order-by=rand --rand-seed=7`: 129/130 casi, **0** `-9`; l'unico
  fallimento è `vulkan_surfaces.cpp:518`.
- `VK_DRIVER_FILES=.../lvp_icd.x86_64.json` (llvmpipe): 130 casi, 129 passati,
  1 fallito, **0** `-9`; 1.931.853 asserzioni (1 fallita).
- Il solo file surfaces: 23 casi, 22 passati, 1 fallito, 0 `-9`; il fallimento
  è la stessa riga 518.

### Misura 2026-11-11 (release, llvmpipe)

`VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json`, binario Release
corrente: **139 casi, 130 passati, 9 falliti, 0 `VkResult=-9`**, 4.458
asserzioni fallite su 1.934.365. Il test GEL composite è stato corretto
(soglia backlit 0.75–0.95, prima 0.45–0.90) e passa 10/10; restano nove casi
effect parity che falliscono anche isolati e con ICD NVIDIA. Il gate 130/130
resta aperto; il `-9` non è più la causa.

### Misura 2026-11-08 (release, llvmpipe)

`VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json`, binario
`chronon3d_backend_registry_tests` del preset release: **134 casi, 123 passati,
11 falliti, 0 `VkResult=-9`**, 4462 assertion fallite su 1.933.817. Il sintomo
di esaurimento istanze è assente; i fallimenti residui sono mismatch reali di
effect parity e surface, non `vkCreateInstance`. Il gate 130/130 resta aperto.

Conclusione: il `-9` non è un tetto del backend né un leak di risorse. In
questa sessione headless l'ICD NVIDIA (`nvidia_icd.json` →
`libGLX_nvidia.so.0`) chiude il proprio contesto driver dopo 26 cicli di
istanza; il loader resta senza ICD e ogni `vkCreateInstance` successiva
fallisce. Su un ICD sano (llvmpipe) la suite arriva a 129/130: l'unica
asserzione rossa è un difetto reale di rilascio delle surface di job.

## Causa e correzione

1. **Surface di job** (`vulkan_surfaces.cpp:518`):
   `NativeEchoSurface::~NativeEchoSurface` chiamava `release_surface`, che
differisce il rilascio quando uno slot del frame batch è ancora in volo; il
lease spariva ma l'handle restava valido. Ora, se dopo `release_surface`
l'handle è ancora valido, il lease chiama
`release_frame_transient_surfaces()` (già virtuale: `wait_for_pending()` +
flush della coda dei rilasci differiti), senza allargare l'interfaccia ABI.
2. **ICD NVIDIA headless**: `libGLX_nvidia.so.0` non regge una suite lunga
senza display; il preset di test deve selezionare un ICD funzionante (llvmpipe
in CI headless, oppure un ICD NVIDIA headless corretto). Non è uno skip: la
suite gira con gli stessi casi e le stesse asserzioni.

## Accettazione

- [x] La misura dice che la risorsa non è trattenuta dal backend: la chiusura
      è del contesto ICD NVIDIA e avviene tutta insieme alla 27ª creazione.
- [ ] `chronon3d_backend_registry_tests` completo gira **130/130 casi, 0
      interrotti** in un solo processo, con lo stesso numero di asserzioni
      verdi di oggi (1.906.529) o superiore.
      *Verifica 2026-11-08: llvmpipe esegue 134 casi senza `-9`; 123 passano e
      11 falliscono su pixel/assertion. Il drain dei lease non è bastato a
      chiudere il gate; il rosso residuo non è più l'esaurimento istanze.*
- [x] Nessun caso viene rimosso, skippato o riordinato per aggirare il limite,
      e nessuno script di misura esporta `VK_ICD_FILENAMES`.

## Riferimenti

- `tests/registry/test_backend_registry_vulkan_effect_parity_test_support.hpp`
  (`shared_vulkan_test_backend`).
- `src/backends/vulkan/vulkan_backend_lifecycle.cpp` (`VulkanBackend::~VulkanBackend`).
