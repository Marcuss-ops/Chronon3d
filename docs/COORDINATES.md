# Chronon3D Coordinate System and Conventions

This document outlines the coordinate conventions, axis orientations, shape positioning, and camera projection math used in the Chronon3D engine. Understanding these conventions is critical for authoring compositions, designing custom rendering backends, and extending the scene builder APIs.

---

## 1. 2D Viewport Space (Screen Space)

In 2D screen/viewport space, Chronon3D follows standard computer graphics and canvas coordinate conventions:

*   **Origin `(0, 0)`**: Located at the **top-left** corner of the composition.
*   **X-axis (Horizontal)**: Points **right** (positive X increases to the right).
*   **Y-axis (Vertical)**: Points **down** (positive Y increases downwards).
*   **Bounds**: Defined by `width` and `height` in the `CompositionSpec` (typically from `(0, 0)` to `(width, height)`).

```
(0, 0) ──────────────────────────► +X (Width)
  │
  │
  │        Viewport Area
  │
  ▼
 +Y (Height)
```

---

## 2. 3D/2.5D World Coordinate Space

Chronon3D employs a unified **Left-Handed Coordinate System (LHS)** for world space positioning. This keeps screen space and world space alignment highly intuitive, as positive directions remain identical for both 2D and 3D:

*   **X-axis (Horizontal)**: Points **right** (+X increases to the right).
*   **Y-axis (Vertical)**: Points **down** (+Y increases downwards).
*   **Z-axis (Depth)**: Points **forward, into the screen** (+Z is deeper into the background; -Z is closer to the camera/viewer).

### Axis Orientation Diagram

```
         (0, 0, 0)
            │
            │  -Z (Towards viewer/camera)
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

> [!NOTE]
> Against a standard right-hand coordinate system, rotations in Chronon3D follow left-handed conventions (the **Left-Hand Rule**).

---

## 3. Camera Positioning & Projection Space

The `Camera2_5D` controls how world-space nodes project onto the 2D viewport. By default, the camera is positioned along the negative Z-axis, pointing towards the origin.

### Default Camera Setup

*   **Camera Position**: `(0.0f, 0.0f, -1000.0f)` (positioned 1000 units in front of the screen).
*   **Target / Point of Interest**: `(0.0f, 0.0f, 0.0f)` (origin).
*   **Default View Vector**: Along the `+Z` direction.

```
       Viewer / Camera
          [Cam] (0, 0, -1000)
            │
            │
  -Z        ▼  View Direction (+Z)
 ───────────┬─────────── (Screen Plane Z = 0)
            │
            │
  +Z        ▼
        Background / Horizon (Z > 0)
```

### Depth and Visibility Rules
*   **Depth Calculation**: The depth of a projected point is measured as its distance from the camera plane along the optical axis.
    *   A point at `z = -500.0f` is closer to the camera (distance = 500), thus has **depth = 500.0f** (closer, rendered on top).
    *   A point at `z = 0.0f` is at the screen plane, thus has **depth = 1000.0f**.
    *   A point at `z = 500.0f` is deeper in the background, thus has **depth = 1500.0f**.
*   **Culling**: Any node or point behind the camera plane (e.g., `z < camera_z`) is automatically culled and not rendered.

---

## 4. Primitive Shape Anchors & Bounds

When defining shapes in `SceneBuilder`, positioning is determined relative to the **shape's center anchor** (with the exception of `line` which uses explicit endpoints).

### 4.1 Rectangles (`Rect` & `RoundedRect`)

*   **Anchor Point**: The **center** of the rectangle.
*   **Parameters**: `.pos` (center coordinates), `.size` (width, height).
*   **Rendered Bounds**: For a rectangle at `pos = (x, y)` with `size = (w, h)`, its boundaries span:
    *   **Horizontal**: `[x - w/2, x + w/2)`
    *   **Vertical**: `[y - h/2, y + h/2)`

```
          x - w/2         x          x + w/2
             ┌────────────┬────────────┐
   y - h/2   │                         │
             │                         │
     y ──────┼──────────(Anchor)───────┤
             │                         │
             │                         │
   y + h/2   └─────────────────────────┘
```

> [!TIP]
> Under Chronon3D's pixel rasterization convention, shapes are rendered using half-open ranges `[min, max)`. A rect at `(50, 50)` with size `(20, 20)` occupies exactly pixels `40` to `59` horizontally and vertically.

---

### 4.2 Circles (`Circle`)

*   **Anchor Point**: The **center** of the circle.
*   **Parameters**: `.pos` (center coordinates), `.radius` (radius).
*   **Mathematical Formula**: A pixel at `(px, py)` is inside the circle if:
    $$\sqrt{(px - x)^2 + (py - y)^2} < \text{radius}$$

```
                ┌─────────┐
             *             *
          *                   *
        *           r           *
       *     (Anchor)────────────*
        *                       *
          *                   *
             *             *
                └─────────┘
```

---

### 4.3 Lines (`Line`)

*   **Anchor Point**: Not applicable.
*   **Parameters**: `.from` (start point), `.to` (end point), `.thickness` (line width).
*   **Rasterization**: Interpolated linearly between `.from` and `.to` using a pixel coverage model matching `.thickness`.

```
        (.from) ────►────────►────► (.to)
        │◄────────── Length ─────────►│
```

---

## 5. Unified Transformations & Parenting

Node transforms (Position, Rotation, Scale) are inherited through the scene hierarchy:

1.  **Parenting**: Child nodes are positioned relative to their parent's coordinate space.
2.  **Order of Operations**: To construct the world matrix of a node, operations are applied in the following order:
    $$\text{WorldMatrix} = \text{ParentWorldMatrix} \times \text{Translation} \times \text{Rotation} \times \text{Scale}$$
3.  **Rotation Axes**: Rotations are specified as Euler angles (in degrees) around the local X, Y, and Z axes.
