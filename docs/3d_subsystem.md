# Chronon3d 3D Subsystem (2.5D Philosophy)

Questo documento delinea l'architettura e le funzionalità del sottosistema 3D di Chronon3d, seguendo la filosofia **2.5D** (After Effects style): livelli piatti posizionati in uno spazio tridimensionale, osservati da una camera virtuale.

---

## 1. Il Modello 2.5D

Chronon3d non è un motore di modellazione 3D (come Blender), ma un motore di **composizione spaziale**. 

### Livelli come Piani
- Ogni `Layer` o `RenderNode` è intrinsecamente 2D (un rettangolo di pixel).
- Abilitando il "3D", il livello guadagna coordinate `Z` e rotazioni `X, Y`.
- Il rendering avviene proiettando questi piani tramite una matrice di trasformazione prospettica.

### Sistema di Coordinate (AE-style)
- **X**: Destra (+), Sinistra (-)
- **Y**: Giù (+), Su (-) [Coerente con il sistema 2D]
- **Z**: Verso l'osservatore (+), Lontano dall'osservatore (-)

---

## 2. La Camera (DLSR Simulator)

La Camera definisce come il mondo 3D viene proiettato sul framebuffer 2D.

### Tipologie
- **One-Node Camera**: Solo posizione e orientamento. Comportamento simile a una telecamera a mano.
- **Two-Node Camera**: Include un **Point of Interest (POI)**. La camera ruota sempre verso il bersaglio, ideale per orbite e tracking.

### Parametri Ottici
- **Zoom / Focal Length**: Definisce l'angolo di campo (FOV). Un 50mm simula una visione naturale, un 18mm deforma prospetticamente i bordi (grandangolo).
- **Depth of Field (DoF)**:
    - **Focus Distance**: La distanza dal piano focale dove gli oggetti sono nitidi.
    - **Aperture**: Controlla l'intensità della sfocatura fuori fuoco.
    - **Blur Level**: Moltiplicatore artistico per la sfocatura.

---

## 3. Illuminazione e Materiali

Le luci interagiscono solo con i livelli che hanno le proprietà 3D abilitate.

### Tipi di Luci
- **Ambient**: Luminosità uniforme su tutta la scena, senza direzione.
- **Parallel**: Simula il sole (raggi paralleli), direzione unica, nessun decadimento.
- **Point**: Luce omnidirezionale (lampadina) con decadimento basato sulla distanza.
- **Spot**: Luce conica con parametri di angolo (`Cone Angle`) e sfumatura dei bordi (`Feather`).

### Proprietà del Materiale (Material Options)
Ogni layer 3D espone parametri di interazione con la luce:
- **Accepts Lights**: Se il layer viene influenzato dalle luci.
- **Accepts Shadows**: Se le ombre degli altri oggetti possono essere proiettate su questo layer.
- **Casts Shadows**: Se il layer proietta la propria ombra.
- **Specular/Shininess**: Definisce quanto il layer è "lucido".

---

## 4. Pipeline di Rendering

### Classic 3D (Fase 1)
- **Z-Sorting**: I livelli vengono ordinati in base alla distanza dalla camera (Z-depth) e disegnati in ordine "pittore" (dal più lontano al più vicino).
- **Transparency**: Gestione perfetta del blending alpha tra piani.
- **Shadow Maps**: Ombre proiettate calcolate tramite mappe di profondità dal punto di vista della luce.

### Advanced 3D (Futuro)
- **Z-Buffer**: Per gestire intersezioni corrette tra piani.
- **PBR (Physically Based Rendering)**: Uso di mappe HDRI (Environment Lights) e materiali fisici (Metallic/Roughness).

---

## 5. Roadmap di Implementazione

1. **Fase 4: Perspective Foundation**
    - Refactor `Camera` per supportare matrici prospettiche.
    - Coordinate Z e rotazioni X/Y nel `Transform`.
2. **Fase 5: Camera Control**
    - Supporto per Two-Node Camera (POI).
    - Integrazione Depth of Field software.
3. **Fase 6: Lighting System**
    - Implementazione luci Point/Spot.
    - Shader di base Lambert/Blinn-Phong per le shape.
4. **Fase 7: Shadow Casting**
    - Proiezione di ombre tra layer 3D.
