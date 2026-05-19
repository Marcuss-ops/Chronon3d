import React from 'react';
import { formatBytes, getFpsColor, getCacheHitColor, renderInfoIcon } from '../utils/format.jsx';

export default function MetricsGrid({ runDetail }) {
  if (!runDetail || !runDetail.run) return null;

  const r = runDetail.run;
  const cacheRateNum = r.cache_hits + r.cache_misses > 0
    ? (r.cache_hits / (r.cache_hits + r.cache_misses) * 100)
    : 0;
  const cacheRateStr = r.cache_hits + r.cache_misses > 0
    ? `${cacheRateNum.toFixed(1)}%`
    : '0.0%';

  return (
    <section className="metrics-grid">
      <div className="glass-panel metric-card">
        <div className="metric-label">
          Effective FPS
          {renderInfoIcon('effective_fps')}
        </div>
        <div className="metric-value" style={{ color: getFpsColor(r.effective_fps) }}>
          {r.effective_fps ? r.effective_fps.toFixed(2) : '0.00'}
          <span className="metric-unit">fps</span>
        </div>
      </div>
      <div className="glass-panel metric-card">
        <div className="metric-label">
          Wall Duration
          {renderInfoIcon('wall_time_ms')}
        </div>
        <div className="metric-value">
          {(r.wall_time_ms / 1000).toFixed(2)}
          <span className="metric-unit">s</span>
        </div>
      </div>
      <div className="glass-panel metric-card">
        <div className="metric-label">
          Peak Memory
          {renderInfoIcon('bytes_allocated_peak')}
        </div>
        <div className="metric-value">
          {formatBytes(r.bytes_allocated_peak)}
        </div>
      </div>
      <div className="glass-panel metric-card">
        <div className="metric-label">
          Cache hit rate
          {renderInfoIcon('cache_hits')}
        </div>
        <div className="metric-value" style={{ color: getCacheHitColor(cacheRateNum) }}>
          {cacheRateStr}
        </div>
      </div>
    </section>
  );
}
