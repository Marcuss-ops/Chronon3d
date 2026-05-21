import React from 'react';
import { formatBytes, getFpsColor, getCacheHitColor, renderInfoIcon } from '../utils/format.jsx';

export default function MetricsGrid({ runDetail }) {
  if (!runDetail || !runDetail.run) return null;

  const r = runDetail.run;
  const frames = runDetail.frames || [];
  const frameCount = frames.length || Number(r.frames_total || 0);
  const wallSeconds = Number(r.wall_time_ms || 0) / 1000;
  const renderSeconds = Number(r.render_ms || 0) / 1000;
  const encodeSeconds = Number(r.encode_ms || 0) / 1000;
  const cacheHits = Number(r.cache_hits || 0);
  const cacheMisses = Number(r.cache_misses || 0);
  const cacheRateNum = cacheHits + cacheMisses > 0
    ? (cacheHits / (cacheHits + cacheMisses) * 100)
    : 0;
  const cacheRateStr = cacheHits + cacheMisses > 0
    ? `${cacheRateNum.toFixed(1)}%`
    : '0.0%';
  const dirtyRatioNum = frames.length > 0
    ? frames.reduce((sum, f) => sum + Number(f.dirty_area_ratio || 0), 0) / frames.length * 100
    : 0;
  const framebufferAllocations = Number(r.framebuffer_allocations || 0);
  const framebufferReuses = Number(r.framebuffer_reuses || 0);
  const framebufferReuseNum = (framebufferAllocations + framebufferReuses) > 0
    ? (framebufferReuses / (framebufferAllocations + framebufferReuses) * 100)
    : 0;

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
          {wallSeconds.toFixed(2)}
          <span className="metric-unit">s</span>
        </div>
      </div>
      <div className="glass-panel metric-card">
        <div className="metric-label">
          Render Duration
          {renderInfoIcon('render_ms')}
        </div>
        <div className="metric-value">
          {renderSeconds.toFixed(2)}
          <span className="metric-unit">s</span>
        </div>
      </div>
      <div className="glass-panel metric-card">
        <div className="metric-label">
          Encode Duration
          {renderInfoIcon('encode_ms')}
        </div>
        <div className="metric-value">
          {encodeSeconds.toFixed(2)}
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
      <div className="glass-panel metric-card">
        <div className="metric-label">
          Dirty Ratio
          {renderInfoIcon('dirty_pixels')}
        </div>
        <div className="metric-value">
          {dirtyRatioNum.toFixed(1)}%
        </div>
      </div>
      <div className="glass-panel metric-card">
        <div className="metric-label">
          Framebuffer Reuse
          {renderInfoIcon('framebuffer_reuses')}
        </div>
        <div className="metric-value">
          {framebufferReuseNum.toFixed(1)}%
        </div>
      </div>
      <div className="glass-panel metric-card">
        <div className="metric-label">
          Frames
          {renderInfoIcon('frames_total')}
        </div>
        <div className="metric-value">
          {frameCount.toLocaleString()}
        </div>
      </div>
    </section>
  );
}
