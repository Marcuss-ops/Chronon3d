export const getAggregatedLayers = (runDetail, selectedFrame) => {
  if (!runDetail || !runDetail.layer_events) return [];
  if (selectedFrame) {
    return runDetail.layer_events.filter(le => le.frame_number === selectedFrame.frame_number);
  }

  const groups = {};
  for (const le of runDetail.layer_events) {
    const id = le.layer_id;
    if (!groups[id]) {
      groups[id] = {
        layer_id: id,
        layer_name: le.layer_name,
        layer_type: le.layer_type,
        durations: [],
        visible_count: 0,
        total_count: 0,
        cull_reasons: new Set(),
        area_pixels: le.area_pixels || 0,
        visible_pixels: [],
        effects: le.effects || '',
        glyphs_rasterized: 0,
        images_sampled: 0
      };
    }
    const g = groups[id];
    g.durations.push(le.duration_ms);
    g.total_count++;
    if (le.visible) {
      g.visible_count++;
    } else if (le.cull_reason) {
      g.cull_reasons.add(le.cull_reason);
    }
    g.visible_pixels.push(le.visible_pixels || 0);
    g.glyphs_rasterized += (le.glyphs_rasterized || 0);
    g.images_sampled += (le.images_sampled || 0);
  }

  return Object.values(groups).map(g => {
    const avgDur = g.durations.reduce((a, b) => a + b, 0) / g.durations.length;
    const avgVisPix = g.visible_pixels.reduce((a, b) => a + b, 0) / g.visible_pixels.length;
    return {
      layer_id: g.layer_id,
      layer_name: g.layer_name,
      layer_type: g.layer_type,
      duration_ms: avgDur,
      visible: g.visible_count > 0,
      cull_rate: 1.0 - (g.visible_count / g.total_count),
      cull_reason: Array.from(g.cull_reasons).filter(Boolean).join(', '),
      area_pixels: g.area_pixels,
      visible_pixels: avgVisPix,
      effects: g.effects,
      glyphs_rasterized: Math.round(g.glyphs_rasterized / g.total_count),
      images_sampled: Math.round(g.images_sampled / g.total_count)
    };
  });
};

export const getAggregatedNodes = (runDetail, selectedFrame) => {
  if (!runDetail || !runDetail.node_events) return [];
  if (selectedFrame) {
    return runDetail.node_events.filter(ne => ne.frame_number === selectedFrame.frame_number);
  }

  const groups = {};
  for (const ne of runDetail.node_events) {
    const key = `${ne.node_name}::${ne.node_type}`;
    if (!groups[key]) {
      groups[key] = {
        node_name: ne.node_name,
        node_type: ne.node_type,
        layer_id: ne.layer_id || '',
        durations: [],
        cache_hits: 0,
        cache_misses: 0,
        cache_bypasses: 0,
        total_count: 0,
        output_width: ne.output_width || 0,
        output_height: ne.output_height || 0,
        output_bytes: ne.output_bytes || 0,
        pixels_touched: 0,
        pixels_cleared: 0,
        pixels_composited: 0,
        pixels_transformed: 0,
        pixels_blurred: 0
      };
    }
    const g = groups[key];
    g.durations.push(ne.duration_ms);
    g.total_count++;
    if (ne.cache_status === 'hit') g.cache_hits++;
    else if (ne.cache_status === 'miss') g.cache_misses++;
    else g.cache_bypasses++;

    g.pixels_touched += (ne.pixels_touched || 0);
    g.pixels_cleared += (ne.pixels_cleared || 0);
    g.pixels_composited += (ne.pixels_composited || 0);
    g.pixels_transformed += (ne.pixels_transformed || 0);
    g.pixels_blurred += (ne.pixels_blurred || 0);
  }

  return Object.values(groups).map(g => {
    const avgDur = g.durations.reduce((a, b) => a + b, 0) / g.durations.length;
    let status = 'mixed';
    if (g.cache_hits === g.total_count) status = 'hit';
    else if (g.cache_misses === g.total_count) status = 'miss';
    else if (g.cache_bypasses === g.total_count) status = 'bypass';

    return {
      node_name: g.node_name,
      node_type: g.node_type,
      layer_id: g.layer_id,
      duration_ms: avgDur,
      executions: g.total_count,
      cache_status: status,
      hit_rate: g.cache_hits / g.total_count,
      output_width: g.output_width,
      output_height: g.output_height,
      output_bytes: g.output_bytes,
      pixels_touched: Math.round(g.pixels_touched / g.total_count),
      pixels_cleared: Math.round(g.pixels_cleared / g.total_count),
      pixels_composited: Math.round(g.pixels_composited / g.total_count),
      pixels_transformed: Math.round(g.pixels_transformed / g.total_count),
      pixels_blurred: Math.round(g.pixels_blurred / g.total_count)
    };
  });
};
