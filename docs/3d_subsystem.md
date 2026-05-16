# Chronon3d 3D Subsystem (2.5D Philosophy)

This document outlines the architecture and features of the Chronon3d 3D subsystem, following the **2.5D** philosophy (After Effects style): flat layers positioned in a three-dimensional space, observed by a virtual camera.

---

## 1. The 2.5D Model

Chronon3d is not a 3D modeling engine (like Blender), but a **spatial composition** engine.

### Layers as Planes
- Every `Layer` or `RenderNode` is inherently 2D (a rectangle of pixels).
- By enabling "3D", the layer gains `Z` coordinates and `X, Y` rotations.
- Rendering occurs by projecting these planes through a perspective transformation matrix.

### Coordinate System (AE-style)
- **X**: Right (+), Left (-)
- **Y**: Down (+), Up (-) [Consistent with the 2D system]
- **Z**: Towards the viewer (+), Away from the viewer (-)

---

## 2. The Camera (DLSR Simulator)

The Camera defines how the 3D world is projected onto the 2D framebuffer.

### Types
- **One-Node Camera**: Position and orientation only. Behaves like a handheld camera.
- **Two-Node Camera**: Includes a **Point of Interest (POI)**. The camera always rotates towards the target, ideal for orbits and tracking.

### Optical Parameters
- **Zoom / Focal Length**: Defines the field of view (FOV). A 50mm lens simulates natural vision, while an 18mm lens perspectively deforms the edges (wide-angle).
- **Depth of Field (DoF)**:
    - **Focus Distance**: The distance from the focal plane where objects are sharp.
    - **Aperture**: Controls the intensity of the out-of-focus blur.
    - **Blur Level**: Artistic multiplier for the blur.

---

## 3. Lighting and Materials

Lights interact only with layers that have 3D properties enabled.

### Types of Lights
- **Ambient**: Uniform brightness across the entire scene, no direction.
- **Parallel**: Simulates the sun (parallel rays), single direction, no decay.
- **Point**: Omnidirectional light (light bulb) with distance-based decay.
- **Spot**: Conical light with `Cone Angle` and edge `Feather` parameters.

### Material Properties (Material Options)
Every 3D layer exposes parameters for light interaction:
- **Accepts Lights**: Whether the layer is affected by lights.
- **Accepts Shadows**: Whether shadows from other objects can be projected onto this layer.
- **Casts Shadows**: Whether the layer projects its own shadow.
- **Specular/Shininess**: Defines how "shiny" the layer is.

---

## 4. Rendering Pipeline

### Classic 3D (Phase 1)
- **Z-Sorting**: Layers are ordered based on their distance from the camera (Z-depth) and drawn in "painter's order" (from farthest to nearest).
- **Transparency**: Perfect handling of alpha blending between planes.
- **Shadow Maps**: Projected shadows calculated via depth maps from the light's point of view.

### Advanced 3D (Future)
- **Z-Buffer**: To handle correct intersections between planes.
- **PBR (Physically Based Rendering)**: Use of HDRI maps (Environment Lights) and physical materials (Metallic/Roughness).

---

## 6. Hierarchy and Resolution

Chronon3d supports parenting between layers and between the camera and layers. This allows for creating complex rigs where the camera follows a layer or is mounted on a "null" for orbiting rotations.

### Camera Parenting
The camera can have a `parent_name`. In this case, its world position and rotation are calculated by summing the parent's transformations.

### Point of Interest (POI) and Target
In addition to manual rotation, the camera can lock onto a `target_name`. If set, the Point of Interest is automatically updated to the world position of the target layer at each frame.

### Helper: `resolve_camera_hierarchy`
For those implementing new backends or render graph passes, a helper is available in `layer_hierarchy.hpp`:

```cpp
#include <chronon3d/scene/layer/layer_hierarchy.hpp>

// Inside the rendering or evaluation loop
auto resolved_cam = resolve_camera_hierarchy(
    scene.layers(),
    scene.resource(),
    scene.camera_2_5d()
);

// resolved_cam.camera contains the final values (resolved position/POI)
// resolved_cam.world_transform contains the complete transformation matrix
```

This pattern ensures that camera behavior is consistent across all rendering paths (Modular Graph, Software Legacy, etc.).
