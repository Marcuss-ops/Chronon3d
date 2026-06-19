// Content registration — single entry point.
// Call register_content_modules(catalog, ctx) once at startup.
// Safe to call multiple times (idempotent).
#pragma once

namespace chronon3d {

class ExtensionCatalog;
struct ExtensionContext;

/// Register all built-in content compositions via the extension system.
///
/// The ExtensionCatalog receives a ContentExtension module that registers
/// every content-domain composition into ctx.compositions.
///
/// Safe to call multiple times (idempotent).
void register_content_modules(ExtensionCatalog& catalog,
                               ExtensionContext& ctx);

} // namespace chronon3d
