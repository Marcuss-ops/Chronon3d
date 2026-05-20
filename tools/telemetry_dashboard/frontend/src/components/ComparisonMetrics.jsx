import React from 'react';
import { formatBytes, getFpsColor, getCacheHitColor, renderInfoIcon } from '../utils/format.jsx';

export default function ComparisonMetrics({ baseRun, compRun }) {
  if (!baseRun || !compRun) return null;

  const getDelta = (curr, prev) => {
    if (!prev) return null;
    const diff = curr - prev;
    const percent = (diff / prev) * 100;
    return { diff, percent };
  };

  const renderDelta = (delta, inverse = false) => {
    if (!delta) return null;
    const isPositive = delta.diff > 0;
    const good = inverse ? !isPositive : isPositive;
    const sign = isPositive ? '+' : '';
    return (
      <span className={`delta-tag ${good ? 'positive' : 'negative'}`}>
        {sign}{delta.percent.toFixed(1)}%
      </span>
    );
  };

  const metrics = [
    {
      label: 'Effective FPS',
      base: baseRun.effective_fps,
      comp: compRun.effective_fps,
      format: (v) => v.toFixed(2) + ' fps',
      color: getFpsColor,
      inverse: false
    },
    {
      label: 'Render Time',
      base: baseRun.render_ms,
      comp: compRun.render_ms,
      format: (v) => (v / 1000).toFixed(2) + ' s',
      inverse: true
    },
    {
      label: 'Peak Memory',
      base: baseRun.bytes_allocated_peak,
      comp: compRun.bytes_allocated_peak,
      format: (v) => formatBytes(v),
      inverse: true
    },
    {
      label: 'Cache Hit Rate',
      base: (baseRun.cache_hits / (baseRun.cache_hits + baseRun.cache_misses || 1)) * 100,
      comp: (compRun.cache_hits / (compRun.cache_hits + compRun.cache_misses || 1)) * 100,
      format: (v) => v.toFixed(1) + '%',
      color: getCacheHitColor,
      inverse: false
    }
  ];

  return (
    <div className="comparison-container">
      <div className="comparison-header">
        <div className="glass-panel" style={{ flex: 1, padding: '12px' }}>
          <div className="label-badge primary" style={{ marginBottom: '4px' }}>Base Run</div>
          <div style={{ fontWeight: 600 }}>{baseRun.composition_id}</div>
          <div style={{ fontSize: '0.75rem', opacity: 0.6 }}>{baseRun.run_id}</div>
        </div>
        <div className="comparison-vs">VS</div>
        <div className="glass-panel" style={{ flex: 1, padding: '12px', borderColor: 'var(--color-accent)' }}>
          <div className="label-badge secondary" style={{ marginBottom: '4px' }}>Comparison</div>
          <div style={{ fontWeight: 600 }}>{compRun.composition_id}</div>
          <div style={{ fontSize: '0.75rem', opacity: 0.6 }}>{compRun.run_id}</div>
        </div>
      </div>

      <div className="metrics-grid">
        {metrics.map((m) => {
          const delta = getDelta(m.comp, m.base);
          return (
            <div key={m.label} className="glass-panel metric-card">
              <div className="metric-label">{m.label}</div>
              <div style={{ display: 'flex', flexDirection: 'column', gap: '4px' }}>
                <div style={{ fontSize: '0.8rem', opacity: 0.5 }}>
                  Base: {m.format(m.base)}
                </div>
                <div className="metric-value" style={{ color: m.color ? m.color(m.comp) : 'inherit', fontSize: '1.3rem' }}>
                  {m.format(m.comp)}
                  {renderDelta(delta, m.inverse)}
                </div>
              </div>
            </div>
          );
        })}
      </div>
    </div>
  );
}
