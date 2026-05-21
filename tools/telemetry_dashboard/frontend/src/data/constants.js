const dashboardHost = typeof window !== 'undefined' ? window.location.hostname : 'localhost';

export const API_BASE = import.meta.env.DEV ? `http://${dashboardHost}:5005` : '';
export const WS_BASE = import.meta.env.DEV ? `http://${dashboardHost}:5005` : window.location.origin;


export const INFO_DESCRIPTIONS = {
  rendering_loop: 'The main rendering loop executing the rendering graph.',
  setup_renderer: 'Time spent setting up context, pipelines, and assets before rendering.',
  Composite: 'Combines individual layers, masks, and backgrounds to form the final frame.',
  Clear: 'Clears the render target/color buffers.',
  Transform: 'Applies 2D/3D matrix transforms (scale, rotate, translate) to layers/objects.',
  EffectStack: 'Evaluates post-processing effects (glow, blur, color adjustments) on the layer stack.',
  bg: 'Renders the composition background.',
  lbl: 'Text layout and glyph rasterized calculations.',
  top_border: 'Draws boundary styling elements or borders.',
  pixels_touched: 'Total number of pixels processed/written across all frames.',
  cache_hits: 'Number of successful cache hits.',
  cache_misses: 'Number of cache misses causing re-evaluation.',
  nodes_executed: 'Total execution count of render graph nodes.',
  layers_rendered: 'Total number of composition layers processed.',
  text_glyphs_rasterized: 'Number of individual font character glyphs rasterized.',
  images_sampled: 'Number of image samples performed by the renderer.',
  blur_pixels: 'Total pixels processed by blur filters.',
  simd_lerp_calls: 'Number of SIMD-assisted lerp/composite fast-path invocations.',
  bytes_allocated_peak: 'Peak system memory allocated during the render run.',
  framebuffer_allocations: 'Total framebuffer allocations performed during the run (per-frame reset).',
  framebuffer_reuses: 'Total framebuffer pool reuses during the run (per-frame reset).',
  framebuffer_bytes_allocated: 'Framebuffer memory allocated during the last frame (reset per-frame, so shows only the current frame allocations). Non-zero means new framebuffers were created despite cache hits.',
  framebuffer_bytes_peak: 'Peak bytes held by framebuffer memory pools during the last frame (reset per-frame).' ,
  dirty_rect_count: 'Number of dirty rectangles emitted during graph execution.',
  dirty_pixels: 'Total pixels covered by dirty rectangles.',
  dirty_full_fallbacks: 'Frames or nodes that fell back to full-frame invalidation.',
  frames_total: 'Number of frames in the render run.',
  effective_fps: 'Frames processed per second including rendering and encoding.',
  wall_time_ms: 'Total clock time elapsed from start to finish.',
  render_ms: 'Core engine CPU/GPU time spent computing/rasterizing frames.',
  encode_ms: 'Time spent encoding the output video.',
  minor_grid: 'Renders the fine grid system layout.',
  major_grid: 'Renders the primary coordinates grid layout.',
  glow_outer: 'Computes glowing particle filters around text or objects.'
};
