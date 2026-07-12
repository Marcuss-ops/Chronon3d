#pragma once
// ═══════════════════════════════════════════════════════════════════════════
//  product_launch.hpp — Source-of-truth 1-line forward decl for the
//  ProductLaunch composition factory.  Reachable directly or via the
//  content/launches/launches.hpp umbrella (mirroring the
//  content/showcases/cinematic/cinematic_text_camera.hpp umbrella pattern).
// ═══════════════════════════════════════════════════════════════════════════
#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::content::launches {

/// Composite end-to-end demo: hero image (manifest-cleansed asset path) +
/// fade-in title + fade-in subtitle + parametric 2.5D camera orbit +
/// cut-crossfade transition at midpoint.  1920×1080 · 90 frames · 30 fps.
///
/// Render (canonical CLI):
///   chronon3d_cli video ProductLaunch -o /tmp/launch.mp4 --start 0 --end 90 \
///                 --fps 30 --motion-blur
Composition product_launch();

} // namespace chronon3d::content::launches
