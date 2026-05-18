# Chronon3d 3D Subsystem (2.5D Philosophy)

This document outlines the architecture and features of the Chronon3d 3D subsystem, following the **2.5D** philosophy (After Effects style): flat layers positioned in a three-dimensional space, observed by a virtual camera.

---

## 1. Coordinate System

### 1.1 2D Viewport Space (Screen Space)

In 2D screen/viewport space, Chronon3d follows standard computer graphics conventions:

- **Origin `(0, 0)`**: Located at the **top-left** corner of the composition.
- **X-axis**: Points **right** (positive X increases to the right).
- **Y-axis**: Points **down** (positive Y increases downwards).
- **Bounds**: Defined by `width` and `height` from `CompositionSpec`.

```
(0, 0) ──────────────────────────► +X (Width)
  │
  │          Viewport Area
  │
  ▼
 +Y (Height)
```

### 1.2 3D/2.5D World Coordinate Space

Chronon3D employs a unified **Left-Handed Coordinate System (LHS)** for world space:

- **X-axis**: Points **right** (+X).
- **Y-axis**: Points **down** (+Y).
- **Z-axis**: Points **forward, into the screen** (+Z is deeper; -Z is closer to camera).

```
         (0, 0, 0)
            │
            │  -Z (Towards viewer)
           .▼.
          /   \
         /     \
────────┼───────► +X (Right)
       / │ \
      /  │  \
     /   ▼   \
   +Z    +Y   -X
 (Into  (Down)
 screen)
```

### 1.3 Camera Positioning

The camera is positioned along the negative Z-axis, pointing towards the origin.

- **Default Camera Position**: `(0.0f, 0.0f, -1000.0f)`
- **Target / Point of Interest**: `(0.0f, 0.0f, 0.0f)`
- **Default View Vector**: Along the `+Z` direction.

**Depth and Visibility Rules:**

- A point at `z = -500.0f` → depth = 500 (closer to camera, rendered on top)
- A point at `z = 0.0f` → depth = 1000 (screen plane)
- A point at `z = 500.0f` → depth = 1500 (deeper in background)
- Any node behind the camera plane is automatically culled.

### 1.4 Primitive Shape Anchors

| Shape | Anchor Point | Notes |
|-------|-------------|-------|
| **Rect / RoundedRect** | Center | Bounds: `[x-w/2, x+w/2)` × `[y-h/2, y+h/2)` |
| **Circle** | Center | Inside if `sqrt((px-x)² + (py-y)²) < radius` |
| **Line** | N/A | Uses explicit `.from` / `.to` endpoints |
| **Text / Image** | Center | Positioned at `.pos` |

### 1.5 Unified Transformations & Parenting

Operations are applied in this order:

$$ \text{WorldMatrix} = \text{ParentWorldMatrix} \times \text{Translation} \times \text{Rotation} \times \text{Scale} $$

Rotations are specified as Euler angles (in degrees) around local X, Y, and Z axes.

---

## 2. The 2.5D Model

Chronon3d is not a 3D modeling engine (like Blender), but a **spatial composition** engine.

### Layers as Planes
- Every `Layer` or `RenderNode` is inherently 2D (a rectangle of pixels).
- By enabling "3D", the layer gains `Z` coordinates and `X, Y` rotations.
- Rendering occurs by projecting these planes through a perspective transformation matrix.

---

## 3. The Camera (DSLR Simulator)

### Types
- **One-Node Camera**: Position and orientation only.
- **Two-Node Camera**: Includes a **Point of Interest (POI)**. Camera always rotates towards target.

### Optical Parameters
- **Zoom / Focal Length**: Defines the field of view (FOV).
- **Depth of Field (DoF)**:
    - **Focus Distance**: Distance from focal plane where objects are sharp.
    - **Aperture**: Controls intensity of out-of-focus blur.
    - **Blur Level**: Artistic multiplier.

---

## 4. Lighting and Materials

### Types of Lights
- **Ambient**: Uniform brightness, no direction.
- **Parallel**: Simulates sun, single direction, no decay.
- **Point** (future): Omnidirectional with decay.
- **Spot** (future): Conical with cone angle and feather.

### Material Properties
- **Accepts Lights**: Whether the layer is affected by lights.
- **Accepts Shadows**: Whether shadows from other objects project onto this layer.
- **Casts Shadows**: Whether the layer projects its own shadow.

---

## 5. Rendering Pipeline

### Classic 3D (Current)
- **Z-Sorting**: Layers ordered by distance from camera, drawn painter's algorithm.
- **Transparency**: Alpha blending between planes.
- **Shadow Maps**: Projected shadows via directional light.

### Advanced 3D (Future)
- **Z-Buffer**: For correct intersections between planes.
- **PBR**: HDRI environment maps, metallic/roughness materials.

---

## 6. Hierarchy and Resolution

Chronon3d supports parenting between layers and between the camera and layers.

### Camera Parenting
The camera can have a `parent_name`. Its world position and rotation are calculated by summing parent transformations.

### Point of Interest and Target
The camera can lock onto a `target_name`. If set, the POI is automatically updated to the target layer's world position at each frame.

### Helper: `resolve_camera_hierarchy`

```cpp
#include <chronon3d/scene/layer/layer_hierarchy.hpp>

auto resolved_cam = resolve_camera_hierarchy(
    scene.layers(),
    scene.resource(),
    scene.camera_2_5d()
);
```

This ensures camera behavior is consistent across all rendering paths.

---

## 7. Current Status

The 2.5D subsystem is functional and production-ready for basic motion graphics.

**What exists:**
- `Camera2_5D` with position, POI, rotation, zoom/FOV, DOF
- `project_layer_2_5d()` for depth, perspective scale, visibility, projected transform
- Rendering: `Rect`, `Image`, `Text`, `FakeBox3D`, `FakeExtrudedText`, `GridPlane`
- Regression tests for transform path coherence, 2.5D projection, compositing, z-order

**Convention:**
- 2D origin: top-left
- X → right, Y → down, Z → into screen (positive = farther)
- Default camera at `z = -1000`
- Screen plane at `z = 0`

**What remains:**
- Single unified projector shared by all software paths
- Less duplication between `FakeBox3D`, `FakeExtrudedText`, `GridPlane`
- Stronger tests on parallax and depth ordering
- Extension to X/Y rotated layers with corner projection, not just perspective scale
- Cleaner separation: layer transform → camera projection → rasterization → compositing

**Definition of "done":**
- `*Unified*` tests stay green
- Camera/projection math lives only in the engine
- Renderers share helpers
- Test files contain no duplicated logic
- Visual behavior is predictable across all paths
