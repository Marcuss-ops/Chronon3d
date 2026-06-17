# Pixel-Ink Centering Postmortem

> Extracted from `docs/ORIENTATION.md` on 2026-06-17.
> Relevant file: `src/backends/text/text_rasterizer_render.cpp`

## Problem

`FontEngine::measure_text()` (FreeType + HarfBuzz) returned widths that differed from what Blend2D actually rasterized.  
Example: "CIAO MONDO" measured ~886px but rendered ~301px — the text appeared shifted by ~289px.

## Solution

After text rendering, a pixel scan finds the actual ink bounds (`ink_left`/`ink_right`/`ink_top`/`ink_bottom`).  
The `x_offset` is then adjusted so the ink center coincides with the box/frame center.

```cpp
// Pseudo-code of the fix:
// 1. Scan rendered pixels, find ink_left and ink_right
// 2. Compute ink_center = (ink_left + ink_right) * 0.5f
// 3. Compute shift = box_center - ink_center
// 4. x_offset += shift  // now the ink is centered
```

## Important for Future Developers

- This fix applies to **both** box centering **and** box-free centering
- Golden image tests must be regenerated after changes to this code
- The fix is in the `t.box.enabled == true` branch (box centering) and the `else` branch (box-free centering)
- The computed offset is cached as part of `TextRasterization`, so the pixel scan runs once per style+text combination
