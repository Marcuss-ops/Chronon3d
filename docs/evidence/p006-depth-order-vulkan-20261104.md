# P0-06 camera-space depth order — 2026-11-04

The image, plane, crossing mesh and 3D text fixture was rendered in all 24
authored layer orders through the C3D CLI on software and on the remote NVIDIA
RTX A4000 Vulkan backend. Each run rendered one 128×96 RGBA frame.

## Results

- Software: 24/24 plans accepted; one unique raw-frame SHA-256 across all
  permutations: `98806c7e584dcd32da04dbc429ce2524139271363dfb294214aa45cc165e71b1`.
- Vulkan/A4000: 24/24 plans accepted; one unique raw-frame SHA-256 across all
  permutations: `703a075c27b0c08667466b81bfc24363ef60a2e9ab3beea888a3551d562d6a76`.
- Every output was exactly 49,152 bytes. The GPU run used the locally built
  CLI (SHA-256
  `62ab4617f9a6efbeb440182b73716c2afb7a6f68eff312c87c14cc43605a20d1`);
  the remote host performed no compilation.
- The fixture contains a 3D image at z=120, opaque plane at z=35, a triangle
  crossing depths 0–650, and 3D text at z=-200. It checks visible text and
  stable pixels while layer order changes.

This certifies local software and remote GPU order invariance for the tested
fixture. It is not same-SHA CI or human visual approval.
