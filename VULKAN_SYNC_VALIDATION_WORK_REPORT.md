# Vulkan sync-validation e rendering: rapporto di lavoro

**Data:** 13 ottobre 2026

**Ambito:** correzioni Vulkan emerse dall'esecuzione delle scene card-stack/documentary, con build, test ed export completi.

## Risultato sintetico

- La validation Vulkan e la synchronization validation sono state abilitate tramite `CHRONON3D_VULKAN_VALIDATION=1` e `CHRONON3D_VULKAN_SYNC_VALIDATION=1`.
- Dopo le correzioni, export completi card-stack raw (due run), card-stack MP4 e documentary MP4 hanno completato 180/180 frame senza messaggi `Vulkan Validation ERROR`, `Vulkan Validation WARNING` o `SYNC-HAZARD` nei rispettivi log.
- Build mirata, cinque suite CTest e `git diff --check` sono passati.
- **Limite aperto:** le due esportazioni raw card-stack non sono bit-deterministiche fra processi distinti. Non è stata identificata una causa dimostrata, quindi il comportamento è riportato come FAIL/non risolto e non è stato introdotto un workaround speculativo.

## Correzioni incluse

### Abilitazione affidabile della validation Vulkan

`src/backends/vulkan/debug/vulkan_debug_context.cpp` ora interroga anche le estensioni esposte dal layer `VK_LAYER_KHRONOS_validation`. In particolare, `VK_EXT_validation_features` può essere fornita dal layer anziché globalmente dal loader: viene ora trovata e attivata in quel caso, evitando inoltre voci duplicate negli elenchi di layer/estensioni. Questo consente al backend di collegare realmente la configurazione di synchronization validation alla creazione dell'istanza.

Il test `tests/backends/test_vulkan_debug_context.cpp` copre la selezione del layer/estensione e crea un'istanza con la feature di synchronization validation quando disponibile.

### Dimensioni buffer Vulkan

`src/backends/vulkan/memory/vulkan_memory_manager.cpp` conserva in `VulkanBufferAllocation::size` la dimensione logica richiesta a `VkBufferCreateInfo`, non la dimensione dell'allocazione VMA sottostante. La dimensione fisica/allineata di VMA non amplia l'intervallo valido del `VkBuffer`; usare quel valore consentiva al codice di ritenere capiente un buffer che non lo era. Il test aggiunto in `tests/backends/test_vulkan_memory_manager.cpp` verifica il contratto con un buffer logico da 152 byte ed è registrato in `tests/shader_abi_tests.cmake`.

La correzione affronta il VUID osservato in un vecchio log documentary (`vkCmdUpdateBuffer` di 160 byte su buffer da 152). Quel log proveniva da un binario precedente alla correzione; documentary è stato quindi rieseguito col binario aggiornato e ha terminato senza messaggi di validation.

### Dipendenze di memoria e dispatch compositing

- `src/backends/vulkan/vulkan_backend_layer_ops_private.cpp`: inserisce una dipendenza compute-write/read-write tra i gruppi di dispatch che compongono un singolo pass e una dipendenza transfer-write→compute-shader-read dopo `vkCmdUpdateBuffer` degli instance data.
- `src/backends/vulkan/vulkan_surface_store_private.cpp`: inserisce la dipendenza transfer-write→transfer-write tra il clear iniziale di un'immagine non inizializzata e il successivo upload buffer-to-image, mantenendo la layout `TRANSFER_DST_OPTIMAL`.
- `tests/backends/test_vulkan_submission_ring.cpp`: aggiunge copertura per readback e ownership fence attorno al ring, gruppi fused di layer a tre sorgenti, trasferimento compositing GPU senza re-upload della mirror CPU e gestione dello slot d'output durante la promozione di una sorgente affine.

### Rendering/compositor

Sono inclusi anche i cambi già presenti nel task per il percorso di compositing e video:

- `src/render_graph/nodes/composite_node.cpp`: evita di usare lo slot fisico d'output come buffer temporaneo di promozione della sorgente affine; conserva il surface handle originale prima che il trasferimento di proprietà lo azzeri, così il percorso nativo non ri-seed-a la destinazione dalla mirror CPU obsoleta.
- `src/media/video/compositor/cuda/cuda_nv12_compositor.cpp`: aggiunge `--std=c++17` sia alle opzioni effettive di NVRTC sia alla chiave d'identità del programma compilato, affinché compilazione e cache fingerprint siano coerenti.

## Verifiche eseguite

### Build e test

Build mirata eseguita sul preset `Chronon3d/build/chronon/linux-video-fast-dev` (Ninja, Debug, Vulkan abilitato):

```bash
cmake --build Chronon3d/build/chronon/linux-video-fast-dev \
  --target chronon3d_cli \
         chronon3d_vulkan_debug_context_tests \
         chronon3d_vulkan_memory_manager_tests \
         chronon3d_vulkan_submission_ring_tests \
         chronon3d_backend_registry_tests \
         chronon3d_shader_abi_consistency_tests -j 4
```

Esito finale: `ninja: no work to do` (tutti i target erano aggiornati). CTest, eseguito dopo il rebuild:

```bash
ctest --test-dir Chronon3d/build/chronon/linux-video-fast-dev \
  --output-on-failure \
  -R 'chronon3d_(vulkan_debug_context|vulkan_memory_manager|vulkan_submission_ring|backend_registry|shader_abi_consistency)_tests?'
```

**Esito: 5/5 PASS** — backend registry, shader ABI consistency, Vulkan debug context, Vulkan memory manager, Vulkan submission ring. La suite backend registry ha richiesto circa 214 s; durata totale circa 224 s.

`git diff --check`, `git diff --cached --check` e il controllo whitespace del nuovo test non tracciato sono passati.

### Export con Vulkan validation + synchronization validation

I quattro export completi sono stati eseguiti tramite il CLI aggiornato con backend Vulkan e gli environment flag di validation/sync-validation impostati. Per ciascun log sono stati verificati layer e debug messenger attivi, 180 frame renderizzati/chiusi e zero occorrenze di warning/error/hazard di validation.

Log e output sono stati conservati in `/tmp/chronon-final-sync/`:

| Prova | Output | Esito validation | SHA-256 |
|---|---|---:|---|
| Card-stack raw run A | `cardstack-a.raw` (1.492.992.000 byte, RGBA, 180 × 1920 × 1080) | 0 messaggi | `12fffc2c92ac24254ba055ed09de69aee903b32cd35bb93061a2c5641fbbc5cc` |
| Card-stack raw run B | `cardstack-b.raw` (1.492.992.000 byte, RGBA, 180 × 1920 × 1080) | 0 messaggi | `6c1a5ec3764a79b728bd294541456726b417db7f23380e5da2865e770e703131` |
| Card-stack video | `cardstack.mp4` | 0 messaggi | `f42b87115ac48ef00c94d988b8cbebe5ef863dced47cb2b7fa18355a48dda3e7` |
| Documentary video | `documentary.mp4` | 0 messaggi | `06172efc0a36678cd6eb3bd8b73acedbb19822fa7036401f3e1de2f3c54b73a7` |

`ffprobe` ha verificato entrambi i video: H.264, 1920×1080, 30/1 fps, 180 frame e durata 6,000 s. Entrambe le scene sono state inoltre provate con smoke Vulkan da 3 frame e zero messaggi di validation.

## Limite: determinismo del raw card-stack

Le due raw complete hanno dimensione corretta e 180 frame, ma hash differenti. Il confronto pixel-per-pixel ha rilevato differenze in **65/180 frame**, pari a circa **27,7 milioni di pixel**; le differenze coinvolgono RGB e non alpha. Due run brevi da 7 frame hanno mostrato frame 0–2 identici e divergenze dal frame 3. Le prove validation restano pulite, ma questo non dimostra determinismo del rendering.

Non è stata individuata una correlazione causale sufficiente per modificare cache, temporal state o rendering dei glyph: nessun fix ulteriore è stato applicato. Il test di determinismo raw è pertanto **FAIL / non risolto**. Il completamento degli MP4 certifica solo render/encode/metadata e pulizia validation, non l'identità tra run.

## File di implementazione e test

- `src/backends/vulkan/debug/vulkan_debug_context.cpp`
- `src/backends/vulkan/memory/vulkan_memory_manager.cpp`
- `src/backends/vulkan/vulkan_backend_layer_ops_private.cpp`
- `src/backends/vulkan/vulkan_surface_store_private.cpp`
- `src/media/video/compositor/cuda/cuda_nv12_compositor.cpp`
- `src/render_graph/nodes/composite_node.cpp`
- `tests/backends/test_vulkan_debug_context.cpp`
- `tests/backends/test_vulkan_memory_manager.cpp` (nuovo)
- `tests/backends/test_vulkan_submission_ring.cpp`
- `tests/shader_abi_tests.cmake`

Il workspace contiene anche modifiche motion-showcase e altri artefatti preesistenti non inclusi in questo rapporto/commit Vulkan; restano intenzionalmente separati.
