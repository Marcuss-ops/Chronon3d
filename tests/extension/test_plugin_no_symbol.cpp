// ── Shared library WITHOUT the chronon3d_module symbol ───────────────────
//
// This file compiles to a .so that exports no ExtensionLoader-visible symbol.
// Used to test that ExtensionLoader::load() correctly fails when the required
// descriptor symbol is missing.
//
// Deliberately DO NOT export any "chronon3d_module" symbol.
// An empty translation unit is valid — the linker still produces a .so.
// Nothing else needed here.
