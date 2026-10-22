# T17 GPU framing iteration — 2026-10-21

This iteration increases the fitted GLB span from 0.72 to 0.90 world units
while retaining the original four subject positions, four focus samples, and
±18° RTX A4000 camera orbit. The 1.20-unit experiment visibly overlapped
subjects and failed the focus-order diagnostic, so it was not retained.

The retained 0.90-unit experiment rendered all 150 frames at 1920×1080/30 fps
through the NVIDIA GL renderer and NVENC. Diagnostic output reports exact
sequential/random frame-77 equality and focus peaks for all four measured
subject ROIs. Review of frames 0, 50, 100, and 149 confirms larger products,
but the last frame clips the near avocado at the left edge and the composition
still has substantial black negative space. This improves visibility but does
not close T17 presentation/occlusion. Metadata and scene JSON preserve the
machine-readable measurements and camera/scene inputs. The video was encoded
on GPU; the separate sharpness pass performs diagnostic CPU readback.
