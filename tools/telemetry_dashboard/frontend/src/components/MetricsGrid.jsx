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
  const framebufferBytesAllocated = Number(r.framebuffer_bytes_allocated || 0);
  const framebufferBytesPeak = Number(r.framebuffer_bytes_peak || 0);

  // Read new framebuffer pipeline counters from the counters array
  const counters = runDetail.counters || [];
  const getCounter = (name) => {
    const found = counters.find(c => c.counter_name === name);
    return found ? Number(found.counter_value) : 0;
  };
  const fbAcquireMs = getCounter('framebuffer_acquire_ms');
  const fbClearMs = getCounter('framebuffer_clear_ms');
  const fbEnqueueMs = getCounter('framebuffer_enqueue_ms');
  const frameConvCopyMs = getCounter('frame_conversion_copy_ms');
  const fbReturnedPool = getCounter('framebuffer_buffer_returned_to_pool_count');
  const fbMissSize = getCounter('framebuffer_pool_miss_count_size_mismatch');
  const fbMissEmpty = getCounter('framebuffer_pool_miss_count_empty');
  const fbMissTotal = fbMissSize + fbMissEmpty;

  return (
    <>
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
            FB Allocations (last frame)
            {renderInfoIcon('framebuffer_bytes_allocated')}
          </div>
          <div className="metric-value">
            {formatBytes(framebufferBytesAllocated)}
          </div>
        </div>
        <div className="glass-panel metric-card">
          <div className="metric-label">
            FB Peak (last frame)
            {renderInfoIcon('framebuffer_bytes_peak')}
          </div>
          <div className="metric-value">
            {formatBytes(framebufferBytesPeak)}
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

      {/* ── Framebuffer Pipeline Diagnostics ── */}
      <h3 className="section-subtitle" style={{ marginTop: '8px', marginBottom: '12px', fontSize: '1rem', fontWeight: 600, color: 'var(--text-secondary)' }}>
        ⚙️ Framebuffer Pipeline Diagnostics
      </h3>
      <section className="metrics-grid">
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-accent)' }}>
          <div className="metric-label">
            FB Acquire Duration
            {renderInfoIcon('framebuffer_acquire_ms')}
          </div>
          <div className="metric-value" style={{ color: fbAcquireMs > 50 ? 'var(--color-danger)' : 'var(--color-accent)' }}>
            {fbAcquireMs.toFixed(2)}
            <span className="metric-unit">ms</span>
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            tempo per acquisire un FB (pool + allocazione)
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-accent)' }}>
          <div className="metric-label">
            FB Clear Duration
            {renderInfoIcon('framebuffer_clear_ms')}
          </div>
          <div className="metric-value" style={{ color: fbClearMs > 30 ? 'var(--color-danger)' : 'var(--color-accent)' }}>
            {fbClearMs.toFixed(2)}
            <span className="metric-unit">ms</span>
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            tempo per pulire memoria FB (fredda vs calda)
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-accent)' }}>
          <div className="metric-label">
            Conversion & Copy
            {renderInfoIcon('frame_conversion_copy_ms')}
          </div>
          <div className="metric-value" style={{ color: frameConvCopyMs > 20 ? 'var(--color-danger)' : 'var(--color-accent)' }}>
            {frameConvCopyMs.toFixed(2)}
            <span className="metric-unit">ms</span>
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            conversione/copia pixel prima dell'encoder
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-accent)' }}>
          <div className="metric-label">
            FB Enqueue Duration
            {renderInfoIcon('framebuffer_enqueue_ms')}
          </div>
          <div className="metric-value" style={{ color: fbEnqueueMs > 30 ? 'var(--color-danger)' : 'var(--color-accent)' }}>
            {fbEnqueueMs.toFixed(2)}
            <span className="metric-unit">ms</span>
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            overhead di rientro FB nel pool
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-accent)' }}>
          <div className="metric-label">
            Pool Miss (size / empty)
            {renderInfoIcon('framebuffer_pool_miss_count_size_mismatch')}
          </div>
          <div className="metric-value" style={{ color: fbMissTotal > 0 ? 'var(--color-danger)' : 'var(--color-success)' }}>
            {fbMissSize.toLocaleString()} / {fbMissEmpty.toLocaleString()}
            <span className="metric-unit">miss</span>
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            dimensioni errate / pool vuoto
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-accent)', gridColumn: 'span 1' }}>
          <div className="metric-label">
            Buffer Returned to Pool
            {renderInfoIcon('framebuffer_buffer_returned_to_pool_count')}
          </div>
          <div className="metric-value" style={{ color: fbReturnedPool === 0 && framebufferReuses > 0 ? 'var(--color-danger)' : 'var(--color-success)' }}>
            {fbReturnedPool.toLocaleString()}
            <span className="metric-unit" style={{ fontSize: '0.65rem' }}>
              {fbReturnedPool === 0 && framebufferReuses > 0 ? ' ⚠️ buffer persi!' : ' riutilizzi'}
            </span>
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            FB rimessi in pool vs distrutti ({framebufferReuses.toLocaleString()} riutilizzi totali)
          </div>
        </div>
      </section>
    </>
  );
}
