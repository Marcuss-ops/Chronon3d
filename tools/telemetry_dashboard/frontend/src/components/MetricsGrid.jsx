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

  // ── Scene Program Cache counters (B6/B8) ─────────────────────────
  const progCacheHits = getCounter('program_cache_hits');
  const progCacheMisses = getCounter('program_cache_misses');
  const progCacheEvictions = getCounter('program_cache_evictions');
  const progCacheCapacity = getCounter('program_cache_capacity');
  const progCacheTune = getCounter('program_cache_tune');
  const progCacheTotal = progCacheHits + progCacheMisses;
  const progCacheHitRate = progCacheTotal > 0
    ? (progCacheHits / progCacheTotal * 100).toFixed(1)
    : '—';
  const fbAcquireMs = getCounter('framebuffer_acquire_ms');
  const fbClearMs = getCounter('framebuffer_clear_ms');
  const fbEnqueueMs = getCounter('framebuffer_enqueue_ms');
  const frameConvCopyMs = getCounter('frame_conversion_copy_ms');
  const fbReturnedPool = getCounter('framebuffer_buffer_returned_to_pool_count');
  const fbEmptyAlloc = getCounter('framebuffer_pool_empty_alloc');
  const fbBestFitReuse = getCounter('framebuffer_pool_best_fit_reuse');
  const fbMissTotal = fbEmptyAlloc + fbBestFitReuse;

  // Benchmark separation counters (per-run totals)
  const videoGraphEvalMs = getCounter('video_graph_eval_ms');
  const videoConversionMs = getCounter('video_conversion_ms');
  const videoPipeWriteMs = getCounter('video_pipe_write_ms');
  const videoFfmpegLatencyMs = getCounter('video_ffmpeg_latency_ms');

  // OS & Process Diagnostics
  const ctxSwitchesVoluntary = getCounter('process_context_switches_voluntary');
  const ctxSwitchesInvoluntary = getCounter('process_context_switches_involuntary');
  const pageFaultsMajor = getCounter('os_page_faults_major');
  const pageFaultsMinor = getCounter('os_page_faults_minor');
  const ffmpegCpuUserPct = getCounter('ffmpeg_cpu_user_pct');
  const ffmpegCpuSysPct = getCounter('ffmpeg_cpu_sys_pct');
  const llcReferences = getCounter('llc_references');
  const llcMisses = getCounter('llc_misses');
  const llcMissRate = llcReferences > 0 ? (llcMisses / llcReferences * 100).toFixed(2) : '—';

  // Setup Deep Dive
  const setupGraphParsingMs = getCounter('setup_graph_parsing_ms');
  const setupAssetIoLoadMs = getCounter('setup_asset_io_load_ms');
  const setupPoolPreallocMs = getCounter('setup_pool_preallocation_ms');
  const imageDecodeMs = getCounter('image_decode_ms');
  const ioQueuePeakBytes = getCounter('io_queue_peak_bytes');

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
            E2E Wall Time
            {renderInfoIcon('wall_time_ms')}
          </div>
          <div className="metric-value">
            {wallSeconds.toFixed(2)}
            <span className="metric-unit">s</span>
          </div>
        </div>
        <div className="glass-panel metric-card">
          <div className="metric-label">
            Chronon Render Time
            {renderInfoIcon('render_ms')}
          </div>
          <div className="metric-value">
            {renderSeconds.toFixed(2)}
            <span className="metric-unit">s</span>
          </div>
        </div>
        <div className="glass-panel metric-card">
          <div className="metric-label">
            FFmpeg Encode Time
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
            Cache Hit Rate
            {renderInfoIcon('cache_hits')}
          </div>
          <div className="metric-value" style={{ color: getCacheHitColor(cacheRateNum) }}>
            {cacheRateStr}
          </div>
        </div>
        <div className="glass-panel metric-card">
          <div className="metric-label">
            Average Dirty Coverage
            {renderInfoIcon('dirty_pixels')}
          </div>
          <div className="metric-value">
            {dirtyRatioNum.toFixed(1)}%
          </div>
        </div>
        <div className="glass-panel metric-card">
          <div className="metric-label">
            Framebuffer Reuse Rate
            {renderInfoIcon('framebuffer_reuses')}
          </div>
          <div className="metric-value">
            {framebufferReuseNum.toFixed(1)}%
          </div>
        </div>
        <div className="glass-panel metric-card">
          <div className="metric-label">
            Framebuffer Bytes Allocated (last frame)
            {renderInfoIcon('framebuffer_bytes_allocated')}
          </div>
          <div className="metric-value">
            {formatBytes(framebufferBytesAllocated)}
          </div>
        </div>
        <div className="glass-panel metric-card">
          <div className="metric-label">
            Framebuffer Bytes Peak (last frame)
            {renderInfoIcon('framebuffer_bytes_peak')}
          </div>
          <div className="metric-value">
            {formatBytes(framebufferBytesPeak)}
          </div>
        </div>
        <div className="glass-panel metric-card">
          <div className="metric-label">
            Frames Total
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
            Framebuffer Acquire Duration
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
            Framebuffer Clear Duration
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
            Frame Conversion & Copy
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
            Framebuffer Enqueue Duration
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
            Framebuffer Pool Misses (empty alloc / best-fit)
            {renderInfoIcon('framebuffer_pool_empty_alloc')}
          </div>
          <div className="metric-value" style={{ color: fbMissTotal > 0 ? 'var(--color-danger)' : 'var(--color-success)' }}>
            {fbEmptyAlloc.toLocaleString()} / {fbBestFitReuse.toLocaleString()}
            <span className="metric-unit">miss</span>
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            alloc da vuoto / riuso best-fit
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-accent)', gridColumn: 'span 1' }}>
          <div className="metric-label">
            Returned to Pool
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
            Render Only
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
            Queue Wait
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
            Chronon Pipeline Total
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
            Render Share of Pipeline
          </div>
          <div className="metric-value" style={{ color: renderPct !== '—' && parseFloat(renderPct) < 50 ? 'var(--color-warning)' : 'var(--color-success)' }}>
            {renderPct === '—' ? '—' : `${renderPct}%`}
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            % del pipeline spesa in rendering puro (vs overhead)
          </div>
        </div>
      </section>

      {/* ── Scene Program Cache (B6/B8) ── */}
      <h3 className="section-subtitle" style={{ marginTop: '24px', marginBottom: '12px', fontSize: '1rem', fontWeight: 600, color: 'var(--text-secondary)' }}>
        🗂️ Scene Program Cache (B6/B8)
      </h3>
      <section className="metrics-grid">
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-success)' }}>
          <div className="metric-label">
            Program Cache Hits
            {renderInfoIcon('program_cache_hits')}
          </div>
          <div className="metric-value" style={{ color: progCacheHits > 0 ? 'var(--color-success)' : 'var(--text-secondary)' }}>
            {progCacheHits.toLocaleString()}
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            programmi trovati in cache (grafo riusato)
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-warning)' }}>
          <div className="metric-label">
            Program Cache Misses
            {renderInfoIcon('program_cache_misses')}
          </div>
          <div className="metric-value" style={{ color: progCacheMisses > 0 ? 'var(--color-warning)' : 'var(--text-secondary)' }}>
            {progCacheMisses.toLocaleString()}
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            compilazioni da zero (struttura cambiata)
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-accent)' }}>
          <div className="metric-label">
            Hit Rate
            {renderInfoIcon('program_cache_hits')}
          </div>
          <div className="metric-value" style={{
            color: progCacheHitRate === '—' ? 'var(--text-secondary)' :
                   parseFloat(progCacheHitRate) > 80 ? 'var(--color-success)' :
                   parseFloat(progCacheHitRate) > 50 ? 'var(--color-warning)' :
                   'var(--color-danger)'
          }}>
            {progCacheHitRate}
            <span className="metric-unit">%</span>
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            {progCacheTotal > 0
              ? `${progCacheHits} hits / ${progCacheTotal} totali`
              : 'nessuna attività cache rilevata'}
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-danger)' }}>
          <div className="metric-label">
            Evictions
            {renderInfoIcon('program_cache_evictions')}
          </div>
          <div className="metric-value" style={{ color: progCacheEvictions > 0 ? 'var(--color-danger)' : 'var(--color-success)' }}>
            {progCacheEvictions.toLocaleString()}
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            entry rimosse per capacità esaurita (LRU)
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-info)', gridColumn: 'span 2' }}>
          <div className="metric-label">
            Ratio Hits / Evictions
          </div>
          <div className="metric-value" style={{ color: progCacheEvictions > 0 && progCacheHits > 0 ? 'var(--color-warning)' : 'var(--color-success)' }}>
            {progCacheEvictions > 0
              ? (progCacheHits / progCacheEvictions).toFixed(1)
              : progCacheHits > 0 ? '∞ (no evictions)' : '—'}
            <span className="metric-unit">×</span>
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            quante hits per ogni eviction. Basso = capacity troppo piccola per il carico di lavoro.
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-accent)' }}>
          <div className="metric-label">
            Cache Capacity
            {renderInfoIcon('program_cache_capacity')}
          </div>
          <div className="metric-value" style={{ color: progCacheCapacity > 0 ? 'var(--color-accent)' : 'var(--text-secondary)' }}>
            {progCacheCapacity > 0 ? progCacheCapacity.toLocaleString() : 'default (8)'}
            <span className="metric-unit">entries</span>
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            dimensione della cache per PrecompNode (configurabile via --program-cache-capacity)
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: progCacheTune > 0 ? '3px solid var(--color-info)' : '3px solid var(--text-muted)' }}>
          <div className="metric-label">
            Tune Mode
          </div>
          <div className="metric-value" style={{ color: progCacheTune > 0 ? 'var(--color-info)' : 'var(--text-secondary)' }}>
            {progCacheTune > 0 ? 'Auto' : 'Fixed'}
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            {progCacheTune > 0
              ? 'capacity regolata automaticamente in base a hit/eviction ratio'
              : 'capacity fissa (modificabile via --program-cache-tune)'}
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
            FFmpeg Backpressure
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

      {/* ── Setup Deep Dive ── */}
      <h3 className="section-subtitle" style={{ marginTop: '24px', marginBottom: '12px', fontSize: '1rem', fontWeight: 600, color: 'var(--text-secondary)' }}>
        🔧 Setup Deep Dive (Cold Start)
      </h3>
      <section className="metrics-grid">
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-info)' }}>
          <div className="metric-label">
            Graph Parsing
            {renderInfoIcon('setup_graph_parsing_ms')}
          </div>
          <div className="metric-value" style={{ color: getDurationColor(setupGraphParsingMs, 200, 1000) }}>
            {setupGraphParsingMs.toFixed(2)}
            <span className="metric-unit">ms</span>
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            tempo per validare/compilare l'albero dei nodi
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-info)' }}>
          <div className="metric-label">
            Asset I/O Load
            {renderInfoIcon('setup_asset_io_load_ms')}
          </div>
          <div className="metric-value" style={{ color: pageFaultsMajor > 0 ? 'var(--color-danger)' : getDurationColor(setupAssetIoLoadMs, 200, 1000) }}>
            {setupAssetIoLoadMs.toFixed(2)}
            <span className="metric-unit">ms</span>
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            caricamento asset da disco (verificare page faults)
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-info)' }}>
          <div className="metric-label">
            Pool Preallocation
            {renderInfoIcon('setup_pool_preallocation_ms')}
          </div>
          <div className="metric-value" style={{ color: getDurationColor(setupPoolPreallocMs, 100, 500) }}>
            {setupPoolPreallocMs.toFixed(2)}
            <span className="metric-unit">ms</span>
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            allocazione iniziale del pool di framebuffer
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-info)' }}>
          <div className="metric-label">
            Image Decode
            {renderInfoIcon('image_decode_ms')}
          </div>
          <div className="metric-value" style={{ color: getDurationColor(imageDecodeMs, 50, 200) }}>
            {imageDecodeMs.toFixed(2)}
            <span className="metric-unit">ms</span>
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            decodifica PNG/JPEG (se applicabile)
          </div>
        </div>
      </section>

      {/* ── OS & Process Diagnostics ── */}
      <h3 className="section-subtitle" style={{ marginTop: '24px', marginBottom: '12px', fontSize: '1rem', fontWeight: 600, color: 'var(--text-secondary)' }}>
        🖥️ OS & Process Diagnostics
      </h3>
      <section className="metrics-grid">
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-warning)' }}>
          <div className="metric-label">
            Page Faults (Major)
            {renderInfoIcon('os_page_faults_major')}
          </div>
          <div className="metric-value" style={{ color: pageFaultsMajor > 0 ? 'var(--color-danger)' : 'var(--color-success)' }}>
            {pageFaultsMajor.toLocaleString()}
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            {pageFaultsMajor > 0 ? '⚠️ Disk I/O in corso!' : 'Nessun major fault'}
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-accent)' }}>
          <div className="metric-label">
            Page Faults (Minor)
            {renderInfoIcon('os_page_faults_minor')}
          </div>
          <div className="metric-value">
            {pageFaultsMinor.toLocaleString()}
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            mappatura memoria (normale in cold start)
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-accent)' }}>
          <div className="metric-label">
            Context Switches (Vol)
            {renderInfoIcon('process_context_switches_voluntary')}
          </div>
          <div className="metric-value">
            {ctxSwitchesVoluntary.toLocaleString()}
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            I/O wait (normale se basso)
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: ctxSwitchesInvoluntary > ctxSwitchesVoluntary ? 'var(--color-danger)' : 'var(--color-accent)' }}>
          <div className="metric-label">
            Context Switches (Invol)
            {renderInfoIcon('process_context_switches_involuntary')}
          </div>
          <div className="metric-value" style={{ color: ctxSwitchesInvoluntary > ctxSwitchesVoluntary * 2 ? 'var(--color-warning)' : 'var(--color-accent)' }}>
            {ctxSwitchesInvoluntary.toLocaleString()}
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            {ctxSwitchesInvoluntary > ctxSwitchesVoluntary * 2 ? '⚠️ CPU contention!' : 'Contenuto'}
          </div>
        </div>
      </section>

      <section className="metrics-grid" style={{ marginTop: '8px' }}>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-info)' }}>
          <div className="metric-label">
            FFmpeg CPU (User)
            {renderInfoIcon('ffmpeg_cpu_user_pct')}
          </div>
          <div className="metric-value" style={{ color: ffmpegCpuUserPct > 80 ? 'var(--color-success)' : ffmpegCpuUserPct > 50 ? 'var(--color-warning)' : 'var(--color-danger)' }}>
            {ffmpegCpuUserPct}
            <span className="metric-unit">%</span>
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            tempo in codice applicativo FFmpeg
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-info)' }}>
          <div className="metric-label">
            FFmpeg CPU (Sys)
            {renderInfoIcon('ffmpeg_cpu_sys_pct')}
          </div>
          <div className="metric-value" style={{ color: ffmpegCpuSysPct > 20 ? 'var(--color-warning)' : 'var(--color-accent)' }}>
            {ffmpegCpuSysPct}
            <span className="metric-unit">%</span>
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            tempo in syscall/kernel (I/O pipe)
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-accent)' }}>
          <div className="metric-label">
            LLC References
            {renderInfoIcon('llc_references')}
          </div>
          <div className="metric-value">
            {llcReferences.toLocaleString()}
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            accessi cache L3 (ultimo livello)
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-accent)' }}>
          <div className="metric-label">
            LLC Miss Rate
            {renderInfoIcon('llc_misses')}
          </div>
          <div className="metric-value" style={{ color: llcMissRate !== '—' && parseFloat(llcMissRate) > 20 ? 'var(--color-danger)' : 'var(--color-success)' }}>
            {llcMissRate}
            <span className="metric-unit">%</span>
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            se alto: memory subsystem in crisi
          </div>
        </div>
      </section>

      {/* ── System Resources & Utilization ── */}
      <h3 className="section-subtitle" style={{ marginTop: '24px', marginBottom: '12px', fontSize: '1rem', fontWeight: 600, color: 'var(--text-secondary)' }}>
        🖥️ System Resources & Utilization
      </h3>
      <section className="metrics-grid">
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-info)' }}>
          <div className="metric-label">
            Logical Cores
          </div>
          <div className="metric-value" style={{ color: 'var(--color-accent)' }}>
            {getCounter('system_logical_cores') || r.cores || '—'}
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            CPU thread disponibili
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-info)' }}>
          <div className="metric-label">
            RAM Total
          </div>
          <div className="metric-value" style={{ color: 'var(--color-accent)' }}>
            {getCounter('system_ram_total_mb') || '—'}
            <span className="metric-unit">MB</span>
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            memoria fisica totale
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-info)' }}>
          <div className="metric-label">
            RAM Available (min)
          </div>
          <div className="metric-value" style={{ color: getCounter('system_ram_available_min_mb') < 1024 ? 'var(--color-danger)' : 'var(--color-success)' }}>
            {getCounter('system_ram_available_min_mb') || '—'}
            <span className="metric-unit">MB</span>
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            minimo disponibile durante il run
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-info)' }}>
          <div className="metric-label">
            Process RSS Peak
          </div>
          <div className="metric-value" style={{ color: 'var(--color-accent)' }}>
            {getCounter('process_rss_peak_mb') || '—'}
            <span className="metric-unit">MB</span>
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            resident set size di picco
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-info)' }}>
          <div className="metric-label">
            Process CPU User
            {renderInfoIcon('process_cpu_user_ms')}
          </div>
          <div className="metric-value">
            {getCounter('process_cpu_user_ms').toFixed(2)}
            <span className="metric-unit">ms</span>
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            CPU utente (per-run delta)
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-info)' }}>
          <div className="metric-label">
            Process CPU Sys
            {renderInfoIcon('process_cpu_sys_ms')}
          </div>
          <div className="metric-value">
            {getCounter('process_cpu_sys_ms').toFixed(2)}
            <span className="metric-unit">ms</span>
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            CPU kernel (per-run delta)
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-info)' }}>
          <div className="metric-label">
            CPU Utilization
            {renderInfoIcon('process_cpu_user_ms')}
          </div>
          <div className="metric-value" style={(() => {
            const cpuMs = getCounter('process_cpu_user_ms') + getCounter('process_cpu_sys_ms');
            const wallMs = Number(r.wall_time_ms || 0);
            const cores = getCounter('system_logical_cores') || 1;
            if (cpuMs > 0 && wallMs > 0) {
              const eff = cpuMs / wallMs;
              const pct = (eff / cores * 100);
              return { color: pct > 70 ? 'var(--color-success)' : pct > 30 ? 'var(--color-warning)' : 'var(--color-danger)' };
            }
            return {};
          })()}>
            {(() => {
              const cpuMs = getCounter('process_cpu_user_ms') + getCounter('process_cpu_sys_ms');
              const wallMs = Number(r.wall_time_ms || 0);
              const cores = getCounter('system_logical_cores') || 1;
              if (cpuMs > 0 && wallMs > 0) {
                const eff = cpuMs / wallMs;
                return (eff / cores * 100).toFixed(1);
              }
              return '—';
            })()}
            <span className="metric-unit">%</span>
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            {(() => {
              const cpuMs = getCounter('process_cpu_user_ms') + getCounter('process_cpu_sys_ms');
              const wallMs = Number(r.wall_time_ms || 0);
              if (cpuMs > 0 && wallMs > 0) {
                return `${(cpuMs / wallMs).toFixed(2)} core equivalenti`;
              }
              return 'N/D';
            })()}
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-accent)' }}>
          <div className="metric-label">
            TBB Arena Max Concurrency
            {renderInfoIcon('tbb_arena_max_concurrency')}
          </div>
          <div className="metric-value" style={{ color: 'var(--color-accent)' }}>
            {getCounter('tbb_arena_max_concurrency') || '—'}
            <span className="metric-unit">workers</span>
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            worker thread TBB configurabili
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-accent)' }}>
          <div className="metric-label">
            TBB Active Workers (Peak)
            {renderInfoIcon('tbb_active_workers_peak')}
          </div>
          <div className="metric-value" style={{ color: getCounter('tbb_active_workers_peak') >= getCounter('system_logical_cores') ? 'var(--color-success)' : 'var(--color-warning)' }}>
            {getCounter('tbb_active_workers_peak') || '—'}
            <span className="metric-unit">workers</span>
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            massimo worker attivi simultaneamente
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-accent)' }}>
          <div className="metric-label">
            TBB Active Workers (Avg)
            {renderInfoIcon('tbb_active_workers_avg_sum')}
          </div>
          <div className="metric-value" style={{ color: 'var(--color-accent)' }}>
            {(() => {
              const sum = getCounter('tbb_active_workers_avg_sum');
              const cnt = getCounter('tbb_active_workers_avg_count');
              if (cnt > 0) return (sum / cnt).toFixed(2);
              return '—';
            })()}
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            media worker attivi per entry parallel_for
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-accent)' }}>
          <div className="metric-label">
            Parallel Regions
            {renderInfoIcon('parallel_regions_count')}
          </div>
          <div className="metric-value" style={{ color: 'var(--color-accent)' }}>
            {getCounter('parallel_regions_count') || '—'}
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            livelli eseguiti in parallelo
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-accent)' }}>
          <div className="metric-label">
            Regions Skipped (≤1 node)
            {renderInfoIcon('parallel_regions_skipped_small_level')}
          </div>
          <div className="metric-value" style={{ color: getCounter('parallel_regions_skipped_small_level') > getCounter('parallel_regions_count') * 5 ? 'var(--color-danger)' : 'var(--color-accent)' }}>
            {getCounter('parallel_regions_skipped_small_level') || '—'}
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            livelli sequenziali (troppo piccoli per parallelizzare)
          </div>
        </div>
      </section>

      {/* ── Parallelism Decisions ── */}
      <h3 className="section-subtitle" style={{ marginTop: '24px', marginBottom: '12px', fontSize: '1rem', fontWeight: 600, color: 'var(--text-secondary)' }}>
        ⚡ Parallelism Decisions
      </h3>
      <section className="metrics-grid">
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-accent)' }}>
          <div className="metric-label">used_parallel_clear</div>
          <div className="metric-value" style={{ color: getCounter('used_parallel_clear') > 0 ? 'var(--color-success)' : 'var(--color-warning)' }}>
            {getCounter('used_parallel_clear') || 0}
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            clear parallele eseguite
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-accent)' }}>
          <div className="metric-label">skipped_clear_small</div>
          <div className="metric-value" style={{ color: getCounter('skipped_clear_small') > 0 ? 'var(--color-warning)' : 'var(--color-success)' }}>
            {getCounter('skipped_clear_small') || 0}
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            clear saltate per area troppo piccola
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-accent)' }}>
          <div className="metric-label">used_parallel_transform</div>
          <div className="metric-value" style={{ color: getCounter('used_parallel_transform') > 0 ? 'var(--color-success)' : 'var(--color-warning)' }}>
            {getCounter('used_parallel_transform') || 0}
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            transform parallele eseguite
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-accent)' }}>
          <div className="metric-label">skipped_transform_small</div>
          <div className="metric-value" style={{ color: getCounter('skipped_transform_small') > 0 ? 'var(--color-warning)' : 'var(--color-success)' }}>
            {getCounter('skipped_transform_small') || 0}
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            transform saltate per area troppo piccola
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-accent)' }}>
          <div className="metric-label">used_parallel_composite</div>
          <div className="metric-value" style={{ color: getCounter('used_parallel_composite') > 0 ? 'var(--color-success)' : 'var(--color-warning)' }}>
            {getCounter('used_parallel_composite') || 0}
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            composite parallele eseguite
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-accent)' }}>
          <div className="metric-label">skipped_composite_small</div>
          <div className="metric-value" style={{ color: getCounter('skipped_composite_small') > 0 ? 'var(--color-warning)' : 'var(--color-success)' }}>
            {getCounter('skipped_composite_small') || 0}
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            composite saltate per row_count &lt; 32
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-accent)' }}>
          <div className="metric-label">Level Parallel</div>
          <div className="metric-value" style={{ color: getCounter('level_parallel_count') > 0 ? 'var(--color-success)' : 'var(--color-warning)' }}>
            {getCounter('level_parallel_count') || 0}
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            livelli con &gt;1 nodo eseguiti in parallelo
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-accent)' }}>
          <div className="metric-label">Level Sequential</div>
          <div className="metric-value" style={{ color: getCounter('level_sequential_count') > 0 ? 'var(--color-accent)' : 'var(--color-success)' }}>
            {getCounter('level_sequential_count') || 0}
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            livelli con ≤1 nodo eseguiti in sequenziale
          </div>
        </div>
      </section>

      {/* ── Phase CPU Efficiency ── */}
      <h3 className="section-subtitle" style={{ marginTop: '24px', marginBottom: '12px', fontSize: '1rem', fontWeight: 600, color: 'var(--text-secondary)' }}>
        📊 Phase CPU Efficiency
      </h3>
      <section className="metrics-grid">
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-info)' }}>
          <div className="metric-label">Sequential Level Exec</div>
          <div className="metric-value">
            {getCounter('sequential_level_execute_ms').toFixed(2)}
            <span className="metric-unit">ms</span>
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            tempo livelli sequenziali
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-info)' }}>
          <div className="metric-label">FB Copy Duration</div>
          <div className="metric-value" style={{ color: getCounter('framebuffer_copy_ms') > 30 ? 'var(--color-danger)' : 'var(--color-accent)' }}>
            {getCounter('framebuffer_copy_ms').toFixed(2)}
            <span className="metric-unit">ms</span>
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            copia pixel framebuffer
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-info)' }}>
          <div className="metric-label">FB Copy Parallel Calls</div>
          <div className="metric-value" style={{ color: getCounter('framebuffer_copy_parallel_calls') > 0 ? 'var(--color-success)' : 'var(--color-accent)' }}>
            {getCounter('framebuffer_copy_parallel_calls') || 0}
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            copie parallele di framebuffer
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-info)' }}>
          <div className="metric-label">Idle Worker Entries</div>
          <div className="metric-value">
            {getCounter('parallel_idle_worker_entry_sum') || 0}
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            osservazioni worker idle
          </div>
        </div>
        <div className="glass-panel metric-card" style={{ borderLeft: '3px solid var(--color-info)' }}>
          <div className="metric-label">Idle Worker Samples</div>
          <div className="metric-value">
            {getCounter('parallel_idle_worker_samples') || 0}
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            campionamenti idle worker
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
            FFmpeg Encode Total (Accumulated)
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
            Legacy Encode Duration
            {renderInfoIcon('encode_ms')}
          </div>
          <div className="metric-value">
            {encodeSeconds.toFixed(2)}
            <span className="metric-unit">s</span>
          </div>
          <div className="metric-sub" style={{ fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: '4px' }}>
            retained only for backwards-compatible comparison
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
