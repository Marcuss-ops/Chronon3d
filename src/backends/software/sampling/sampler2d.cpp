// ---------------------------------------------------------------------------
// sampler2d.cpp — Sampler2D implementation
// ---------------------------------------------------------------------------
//
// Currently all Sampler2D methods are defined inline in sampler2d.hpp.
// This translation unit exists for:
//   a) Future non-inline methods (Lanczos, bicubic, mip-mapping, etc.)
//   b) Explicit template instantiations if needed
//   c) Avoiding linker warnings about inline functions in TU-less .cppm

#include <chronon3d/backends/software/sampling/sampler2d.hpp>
