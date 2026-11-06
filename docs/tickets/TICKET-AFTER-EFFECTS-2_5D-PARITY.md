# TICKET: Parità After Effects vs Chronon3D (2.5D Motion Graphics & Volumetric Gel Surface)

**Stato**: IN PROGRESS (Fase 2.5D Camera & Abstract Compositor sbloccata)
**Data**: 5 Novembre 2026
**Ambito**: Chronon3D Rendering Engine / Vulkan GPU Hot-Path

---

## 1. Obiettivo

Confrontare in modo rigoroso e trasparente le capacità del motore **Chronon3D** rispetto allo standard industriale di **Adobe After Effects (AE)** per la produzione di motion graphics cinematografica con:
- Testo lucido volumetrico / superficie gel con cupola SDF e bloom HDR;
- Spazio compositivo 2.5D multi-piano con parallasse di profondità;
- Cameraing cinematografico orbitale con Depth of Field (DoF);
- Sfondi astratti procedurali (aurore, nastri al neon, sfere di vetro, bagliori).

---

## 2. Matrice di Parità: Cosa Abbiamo già Identico ad After Effects

| Funzionalità | After Effects | Chronon3D | Note Implementative Chronon |
|---|---|---|---|
| **Layer 2.5D Multi-Piano** | Interruttore cubetto 3D (`Enable 3D Layer`) | `"enable_3d": true` | Coordinate `[X, Y, Z]`, rotazioni Euler/quaternion `[Pitch, Yaw, Roll]`, matrice omografica proiettiva. |
| **Camera Prospettica Cinematografica** | Camera Layer (One-Node / Two-Node) | `"camera"` + `"camera_samples"` | Campionamento fluido di posizione, mira, rotazione, rollio olandese (`roll_deg`), FOV dinamico. |
| **Profondità di Campo (Depth of Field)** | Camera Options -> Depth of Field ON | `camera.dof: { "enabled": true, ... }` | Calcolo del circolo di confusione (CoC) per strato in base a `focus_distance`, `f_stop`, `max_blur` con sfocatura bokeh. |
| **Modi di Fusione dei Livelli** | Blend Modes (Screen, Add, Multiply, ecc.) | `"blend_mode": "screen" / "add"` | Propagato attraverso `StaticLayerProgram` -> `SceneMaterializer` -> `CompositeNode`. |
| **Matte di Traccia (Track Matte)** | Track Matte (Alpha, Inverted, Luma) | `"track_matte": { "type": "alpha", ... }` | `TrackMatteNode` in GPU Vulkan / Software. |
| **Animatori Tipografici per-glifo** | Text Layer -> Animate -> Tracking/Position | `"text_animators": [ ... ]` | Campionamento per-carattere o percentuale con easing e proprietà personalizzate. |
| **Superficie Bombata Gel/Glossy** | *Non nativo* (richiede plugin terze parti come Element 3D o trapcode) | **Nativo GPU Vulkan** (`gel_surface: true`) | Calcolo cupola SDF su TextRun composito (`text_tile_raster.comp`), ricostruzione normale analitica, speculare e rim light. |
| **Bloom / Glow Spettrale a Doppio Alone** | Deep Glow / Glow nativo con 2 passaggi | `EffectStackNode`: doppio Glow (stretto ciano + largo blu) | Preservazione del corpo pieno senza sbiancamento o scomparsa del nucleo del testo. |

---

## 3. Cosa ci Manca Ancora Rispetto ad After Effects (Gap Analysis)

### Gap 1 — Motion Blur Dinamico da Movimento di Camera 2.5D
- **In After Effects**: Attivando l'icona del Motion Blur sul layer e sulla composizione, qualsiasi camera dolly o rotazione rapida genera automaticamente un motion blur basato su Shutter Angle (es. 180°) campionando sub-frame intermedi.
- **In Chronon3D**: Abbiamo il campionamento sub-frame `temporal_motion_blur` e motion vector per alcune pipeline mesh, ma non ancora l'accumulazione automatica di shutter blur generalizzata a tutti i nodi di trasformazione 2.5D nei movimenti di camera ad altissima velocità.
- **Soluzione prevista**: Espandere il pass di risoluzione camera con accumulatore temporale o pass post-processing a vector blur calcolato dalla matrice delta camera.

### Gap 2 — Ombre Portate Direzionali da Luce Punctiforme/Spot (Cast Shadows)
- **In After Effects**: Se si crea una `Point Light` o `Spot Light`, i layer 2.5D proiettano ombre vettoriali realistiche sui layer retrostanti nello spazio 3D.
- **In Chronon3D**: Abbiamo il `shadow_node` e il lighting rig direzionale per mesh, ma non ancora la proiezione di shadow map conica tra layer vettoriali 2.5D planari arbitrari.
- **Soluzione prevista**: Abilitare il pass shadow-map con rasterizzazione dell'alpha-matte del caster su un buffer di profondità relativo al receiver.

### Gap 3 — Rifrazione e Spessore Fisico Interno (Index of Refraction / IOR)
- **In After Effects**: Con il renderer nativo Classic 3D non è possibile; con il motore Cinema 4D integrato o plugin come Glass/Raytracer si ottiene la rifrazione ottica dell'ambiente retrostante.
- **In Chronon3D**: Il nostro shader gel (`text_tile_raster.comp`) calcola la normale superficiale e la luce incidente/speculare/rim con grandissimo impatto visivo, ma non campiona lo sfondo sottostante deviando i raggi secondo la legge di Snell (rifrazione con indice IOR ~1.45).
- **Soluzione prevista**: Introdurre un pass di distorsione per rifrazione nel compositing finale del gel text campionando la texture dello sfondo con offset proporzionale alla normale $N_{xy}$.

### Gap 4 — Interfaccia Grafica (GUI) Interattiva
- **In After Effects**: Manipolazione visiva con mouse, timeline, pannello curve grafiche.
- **In Chronon3D**: È un engine headless orientato al rendering ad altissima velocità su server/GPU (JSON render plans). L'anteprima si controlla tramite i comandi CLI e le pipeline automatiche.

---

## 4. Stato della Patch Corrente

Nel commit locale in fase di compilazione:
1. `blend_mode` e `screen_space` integrati in `compiled_layer.hpp` e propagati a `SceneMaterializer`.
2. Risolto il tracciamento dinamico di `world_z` durante le animazioni per consentire il DoF ottico corretto.
3. Garantita la residency nativa Vulkan GPU per i layer proiettati 2.5D.
4. Generato piano di verifica a 1080p: `gel-25d-abstract-camera-1080p.plan.json`.
