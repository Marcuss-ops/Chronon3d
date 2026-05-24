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

  // Benchmark separation counters (per-run totals)
  const videoGraphEvalMs = getCounter('video_graph_eval_ms');
  const videoConversionMs = getCounter('video_conversion_ms');
  const videoPipeWriteMs = getCounter('video_pipe_write_ms');
  const videoFfmpegLatencyMs = getCounter('video_ffmpeg_latency_ms');

  // Nuove metriche benchmark separation
  const chrononRenderOnlyMs = Number(r.chronon_render_only_ms || 0);
  const chrononConvCopyMs = Number(r.chronon_conversion_copy_ms || 0);
  const chrononQueueWaitMs = Number(r.chronon_queue_wait_ms || 0);
  const chrononPipelineTotalMs = chrononRenderOnlyMs + chrononConvCopyMs + chrononQueueWaitMs;
  const renderPct = chrononPipelineTotalMs > 0
    ? (chrononRenderOnlyMs / chrononPipelineTotalMs * 100).toFixed(1)
    : '—';
  const ffmpegEncodeTotalMs = Number(r.ffmpeg_encode_total_ms || 0);
  const ffmpegFlushCloseMs = Number(r.ffmpeg_flush_close_ms || 0);
  const e2eWallMs = Number(r.e2e_wall_ms || 0);
  const encodeToWallPct = e2eWallMs > 0
    ? (ffmpegEncodeTotalMs / e2eWallMs * 100).toFixed(1)
    : '—';
  const renderToEncodeRatio = ffmpegEncodeTotalMs > 0 && chrononPipelineTotalMs > 0
    ? (ffmpegEncodeTotalMs / chrononPipelineTotalMs).toFixed(2)
    : '—';

  const getDurationColor = (ms, warnThreshold, dangerThreshold) => {
    if (ms > dangerThreshold) return 'var(--color-danger)';
    if (ms > warnThreshold) return 'var(--color-warning)';
    return 'var(--color-success)';
  };

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

      {/* ── Chronon Render Throughput Benchmark ── */}
      <h3 className="section-subtitle" style={{ marginTop: '24px', marginBottom: '12px', fontSize: '1rem', fontWeight: 600, color: 'var(--text-secondary)' }}>
        ⏱️ Chronon Render Throughput
      </h3>
      <section className="metrics-grid">
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-info)' }}>
          <div className="metric-label">
            Render Only (puro)
            {renderInfoIcon('chronon_render_only_ms')}
          </div>
          <div className="metric-value" style={{ color: getDurationColor(chrononRenderOnlyMs, 50, 200) }}>
            {chrononRenderOnlyMs.toFixed(2)}
            <span className="metric-unit">ms</span>
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            solo grafo + cache + rasterizzazione (no encoder)
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-info)' }}>
          <div className="metric-label">
            Conversion & Copy
            {renderInfoIcon('chronon_conversion_copy_ms')}
          </div>
          <div className="metric-value" style={{ color: getDurationColor(chrononConvCopyMs, 10, 50) }}>
            {chrononConvCopyMs.toFixed(2)}
            <span className="metric-unit">ms</span>
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            RGBA→YUV + copy queue prima dell'encoder
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-info)' }}>
          <div className="metric-label">
            Queue Wait (backpressure)
            {renderInfoIcon('chronon_queue_wait_ms')}
          </div>
          <div className="metric-value" style={{ color: getDurationColor(chrononQueueWaitMs, 5, 30) }}>
            {chrononQueueWaitMs.toFixed(2)}
            <span className="metric-unit">ms</span>
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            attesa coda quando encoder è in affanno
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-info)' }}>
          <div className="metric-label">
            Pipeline Total
            {renderInfoIcon('chronon_render_throughput_ms')}
          </div>
          <div className="metric-value" style={{ color: getDurationColor(chrononPipelineTotalMs, 100, 400) }}>
            {chrononPipelineTotalMs.toFixed(2)}
            <span className="metric-unit">ms</span>
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            render + conversion + queue = throughput Chronon puro
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-info)' }}>
          <div className="metric-label">
            Render % of Pipeline
          </div>
          <div className="metric-value" style={{ color: renderPct !== '—' && parseFloat(renderPct) < 50 ? 'var(--color-warning)' : 'var(--color-success)' }}>
            {renderPct === '—' ? '—' : `${renderPct}%`}
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            % del pipeline spesa in rendering puro (vs overhead)
          </div>
        </div>
      </section>

      {/* ── Benchmark Breakdown (Per-Run Counters) ── */}
      <h3 className="section-subtitle" style={{ marginTop: '24px', marginBottom: '12px', fontSize: '1rem', fontWeight: 600, color: 'var(--text-secondary)' }}>
        📊 Benchmark Breakdown (Per-Run)
      </h3>
      <section className="metrics-grid">
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-accent)' }}>
          <div className="metric-label">
            Graph Eval
            {renderInfoIcon('video_graph_eval_ms')}
          </div>
          <div className="metric-value" style={{ color: getDurationColor(videoGraphEvalMs, 100, 500) }}>
            {videoGraphEvalMs.toFixed(2)}
            <span className="metric-unit">ms</span>
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            tempo per decidere cosa renderizzare (grafo + scene eval)
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-accent)' }}>
          <div className="metric-label">
            Conversion (SIMD)
            {renderInfoIcon('video_conversion_ms')}
          </div>
          <div className="metric-value" style={{ color: getDurationColor(videoConversionMs, 20, 100) }}>
            {videoConversionMs.toFixed(2)}
            <span className="metric-unit">ms</span>
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            kernel SIMD RGBA→YUV
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-accent)' }}>
          <div className="metric-label">
            Pipe Write
            {renderInfoIcon('video_pipe_write_ms')}
          </div>
          <div className="metric-value" style={{ color: getDurationColor(videoPipeWriteMs, 50, 200) }}>
            {videoPipeWriteMs.toFixed(2)}
            <span className="metric-unit">ms</span>
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            trasferimento dati alla pipe FFmpeg (fwrite)
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-accent)' }}>
          <div className="metric-label">
            FFmpeg Latency
            {renderInfoIcon('video_ffmpeg_latency_ms')}
          </div>
          <div className="metric-value" style={{ color: getDurationColor(videoFfmpegLatencyMs, 20, 100) }}>
            {videoFfmpegLatencyMs.toFixed(2)}
            <span className="metric-unit">ms</span>
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            backpressure: FFmpeg non ha ancora svuotato il buffer
          </div>
        </div>
      </section>

      {/* ── End-to-End Export Benchmark ── */}
      <h3 className="section-subtitle" style={{ marginTop: '24px', marginBottom: '12px', fontSize: '1rem', fontWeight: 600, color: 'var(--text-secondary)' }}>
        📦 End-to-End Export Benchmark
      </h3>
      <section className="metrics-grid">
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-warning)' }}>
          <div className="metric-label">
            FFmpeg Encode Total
            {renderInfoIcon('ffmpeg_encode_total_ms')}
          </div>
          <div className="metric-value" style={{ color: getDurationColor(ffmpegEncodeTotalMs, 500, 2000) }}>
            {ffmpegEncodeTotalMs.toFixed(2)}
            <span className="metric-unit">ms</span>
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            tempo totale dentro FFmpeg (codec + muxing)
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-warning)' }}>
          <div className="metric-label">
            FFmpeg Flush/Close
            {renderInfoIcon('ffmpeg_flush_close_ms')}
          </div>
          <div className="metric-value" style={{ color: getDurationColor(ffmpegFlushCloseMs, 100, 500) }}>
            {ffmpegFlushCloseMs.toFixed(2)}
            <span className="metric-unit">ms</span>
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            flush finale + chiusura container
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-warning)' }}>
          <div className="metric-label">
            Encode Duration (legacy)
            {renderInfoIcon('encode_ms')}
          </div>
          <div className="metric-value">
            {encodeSeconds.toFixed(2)}
            <span className="metric-unit">s</span>
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            metrica legacy per confronto
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-warning)' }}>
          <div className="metric-label">
            E2E Wall Time
            {renderInfoIcon('e2e_wall_ms')}
          </div>
          <div className="metric-value" style={{ color: getDurationColor(e2eWallMs, 1000, 5000) }}>
            {e2eWallMs > 0 ? (e2eWallMs / 1000).toFixed(2) : wallSeconds.toFixed(2)}
            <span className="metric-unit">s</span>
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            wall clock totale dall'inizio alla fine
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-warning)' }}>
          <div className="metric-label">
            Encode % of E2E
          </div>
          <div className="metric-value" style={{ color: encodeToWallPct !== '—' && parseFloat(encodeToWallPct) > 70 ? 'var(--color-danger)' : 'var(--color-warning)' }}>
            {encodeToWallPct === '—' ? '—' : `${encodeToWallPct}%`}
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            % del wall time dominata da FFmpeg
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-warning)' }}>
          <div className="metric-label">
            Render : Encode Ratio
          </div>
          <div className="metric-value" style={{ color: renderToEncodeRatio !== '—' && parseFloat(renderToEncodeRatio) > 2 ? 'var(--color-danger)' : 'var(--color-success)' }}>
            {renderToEncodeRatio === '—' ? '—' : `1 : ${renderToEncodeRatio}`}
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            bilanciamento: chronon pipeline vs encode (alto = FFmpeg domina)
          </div>
        </div>
      </section>
    </>
  );
}
