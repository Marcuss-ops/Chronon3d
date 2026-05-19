import React, { useState, useEffect, useRef } from 'react';
import './App.css';

// Pre-built analysis queries from queries.sql
const QUERY_PRESETS = [
  {
    name: 'Run Summary Stats',
    query: `SELECT 
    run_id,
    composition_id,
    success,
    frames_total,
    frames_written,
    wall_time_ms,
    render_ms,
    effective_fps,
    (bytes_allocated_peak / 1024.0 / 1024.0) AS peak_mem_mb,
    finished_at_iso
FROM render_runs
ORDER BY finished_at_iso DESC;`
  },
  {
    name: 'Cache Efficiency',
    query: `SELECT 
    run_id,
    composition_id,
    cache_hits,
    cache_misses,
    CAST(cache_hits AS REAL) / (cache_hits + cache_misses) * 100.0 AS node_cache_hit_rate_pct,
    tiles_hit,
    tiles_miss,
    CAST(tiles_hit AS REAL) / (tiles_hit + tiles_miss) * 100.0 AS tiles_cache_hit_rate_pct,
    node_cache_hash_collisions
FROM render_runs
WHERE (cache_hits + cache_misses) > 0;`
  },
  {
    name: 'Node Bottlenecks',
    query: `SELECT 
    run_id,
    REPLACE(phase_name, 'node:', '') AS node_name,
    duration_ms AS total_node_dur_ms,
    duration_ms / (SELECT duration_ms FROM render_phase_events WHERE run_id = p.run_id AND phase_name = 'rendering_loop') * 100.0 AS percentage_of_loop_pct
FROM render_phase_events p
WHERE phase_name LIKE 'node:%'
ORDER BY duration_ms DESC;`
  },
  {
    name: 'Slowest Frame Analysis',
    query: `SELECT 
    f.run_id,
    r.composition_id,
    f.frame_number,
    f.duration_ms,
    f.cache_hit,
    f.dirty_area_ratio
FROM render_frames f
JOIN render_runs r ON f.run_id = r.run_id
ORDER BY f.duration_ms DESC
LIMIT 10;`
  }
];

const INFO_DESCRIPTIONS = {
  rendering_loop: "The main rendering loop executing the rendering graph.",
  setup_renderer: "Time spent setting up context, pipelines, and assets before rendering.",
  Composite: "Combines individual layers, masks, and backgrounds to form the final frame.",
  Clear: "Clears the render target/color buffers.",
  Transform: "Applies 2D/3D matrix transforms (scale, rotate, translate) to layers/objects.",
  EffectStack: "Evaluates post-processing effects (glow, blur, color adjustments) on the layer stack.",
  bg: "Renders the composition background.",
  lbl: "Text layout and glyph rasterized calculations.",
  top_border: "Draws boundary styling elements or borders.",
  pixels_touched: "Total number of pixels processed/written across all frames.",
  cache_hits: "Number of successful cache hits.",
  cache_misses: "Number of cache misses causing re-evaluation.",
  nodes_executed: "Total execution count of render graph nodes.",
  layers_rendered: "Total number of composition layers processed.",
  text_glyphs_rasterized: "Number of individual font character glyphs rasterized.",
  blur_pixels: "Total pixels processed by blur filters.",
  bytes_allocated_peak: "Peak system memory allocated during the render run.",
  effective_fps: "Frames processed per second including rendering and encoding.",
  wall_time_ms: "Total clock time elapsed from start to finish.",
  render_ms: "Core engine CPU/GPU time spent computing/rasterizing frames.",
  minor_grid: "Renders the fine grid system layout.",
  major_grid: "Renders the primary coordinates grid layout.",
  glow_outer: "Computes glowing particle filters around text or objects."
};

const API_BASE = 'http://localhost:8000';

const outputPathToArtifactUrl = (outputPath) => {
  if (!outputPath) return '';
  return `${API_BASE}/artifact?path=${encodeURIComponent(outputPath)}`;
};

const isVideoOutput = (outputPath) => /\.(mp4|webm|mov)$/i.test(outputPath || '');
const isImageOutput = (outputPath) => /\.(png|jpg|jpeg|webp|gif|svg)$/i.test(outputPath || '');

function App() {
  const [runs, setRuns] = useState([]);
  const [selectedRunId, setSelectedRunId] = useState('');
  const [runDetail, setRunDetail] = useState(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState('');

  // Use a ref to track the currently selected run ID to avoid stale closures in setInterval
  const selectedRunIdRef = useRef(selectedRunId);
  useEffect(() => {
    selectedRunIdRef.current = selectedRunId;
  }, [selectedRunId]);

  // Sidebar collapsible state
  const [sidebarCollapsed, setSidebarCollapsed] = useState(false);

  // Selected frame interactive state
  const [selectedFrame, setSelectedFrame] = useState(null);
  const [hoveredFrame, setHoveredFrame] = useState(null);

  // Reset selected frame when run changes
  useEffect(() => {
    setSelectedFrame(null);
  }, [selectedRunId]);

  // Copy buttons state
  const [copiedPhases, setCopiedPhases] = useState(false);
  const [copiedMetrics, setCopiedMetrics] = useState(false);

  useEffect(() => {
    fetchRuns();
    // Poll for new runs and update currently selected run details every 3 seconds
    const interval = setInterval(async () => {
      try {
        const res = await fetch(`${API_BASE}/api/runs`);
        if (res.ok) {
          const data = await res.json();
          setRuns(data);
          setSelectedRunId(prev => {
            if (prev && data.some(r => r.run_id === prev)) {
              return prev;
            }
            return data.length > 0 ? data[0].run_id : '';
          });
        }
        
        // Also silently fetch details of the currently selected run to keep it updated in real-time
        if (selectedRunIdRef.current) {
          fetchRunDetail(selectedRunIdRef.current, true);
        }
      } catch (err) {
        console.error('Polling error:', err);
      }
    }, 3000);
    return () => clearInterval(interval);
  }, []);

  useEffect(() => {
    if (selectedRunId) {
      fetchRunDetail(selectedRunId);
    }
  }, [selectedRunId]);

  const fetchRuns = async () => {
    try {
      setLoading(true);
      const res = await fetch(`${API_BASE}/api/runs`);
      if (!res.ok) throw new Error('Failed to load runs');
      const data = await res.json();
      setRuns(data);
      setSelectedRunId(prev => {
        if (prev && data.some(r => r.run_id === prev)) {
          return prev;
        }
        return data.length > 0 ? data[0].run_id : '';
      });
      setError('');
    } catch (err) {
      setError(err.message);
    } finally {
      setLoading(false);
    }
  };

  const fetchRunDetail = async (id, silent = false) => {
    try {
      if (!silent) setLoading(true);
      const res = await fetch(`${API_BASE}/api/run/${id}`);
      if (!res.ok) throw new Error('Failed to load run details');
      const data = await res.json();
      setRunDetail(data);
      if (!silent) setError('');
    } catch (err) {
      if (!silent) setError(err.message);
    } finally {
      if (!silent) setLoading(false);
    }
  };

  const copyPhasesToClipboard = () => {
    if (!runDetail || !runDetail.phases) return;
    const text = runDetail.phases.map(p => {
      const isNode = p.phase_name.startsWith('node:');
      const name = isNode ? p.phase_name.substring(5) : p.phase_name;
      return `${name}: ${p.duration_ms.toFixed(2)} ms`;
    }).join('\n');
    navigator.clipboard.writeText(text);
    setCopiedPhases(true);
    setTimeout(() => setCopiedPhases(false), 2000);
  };

  const copyMetricsToClipboard = () => {
    if (!runDetail || !runDetail.run) return;
    const r = runDetail.run;
    const cacheRate = r.cache_hits + r.cache_misses > 0 
      ? `${(r.cache_hits / (r.cache_hits + r.cache_misses) * 100).toFixed(1)}%`
      : '0.0%';

    // Gather phases
    const corePhasesText = runDetail.phases
      .filter(p => !p.phase_name.startsWith('node:'))
      .map(p => `- **${p.phase_name}**: ${p.duration_ms.toFixed(2)} ms`)
      .join('\n');

    const nodePhasesText = runDetail.phases
      .filter(p => p.phase_name.startsWith('node:'))
      .map(p => `- **${p.phase_name.replace('node:', '')}**: ${p.duration_ms.toFixed(2)} ms`)
      .join('\n');

    // Gather counters
    const countersText = runDetail.counters && runDetail.counters.length > 0
      ? runDetail.counters.map(c => `- **${c.counter_name.replace(/_/g, ' ')}**: ${formatCounterValue(c.counter_name, c.counter_value)}`).join('\n')
      : 'None';

    const text = `# Chronon3D Telemetry Report - ${r.composition_id}

## Composition & Environment
- **Composition**: ${r.composition_id}
- **Run ID**: ${r.run_id}
- **Status**: ${r.success ? 'SUCCESS' : 'FAILED'}
- **Finished At**: ${formatIso(r.finished_at_iso)}
- **Git Commit**: ${r.git_commit_short}
- **Build**: ${r.build_type} (${r.compiler_info})
- **OS/CPU**: ${r.os} / ${r.cpu_model} (${r.cores} cores)

## Performance Metrics
- **Effective FPS**: ${r.effective_fps ? r.effective_fps.toFixed(2) : '0.00'} fps
- **Wall Duration**: ${(r.wall_time_ms / 1000).toFixed(2)} s
- **Render Duration**: ${(r.render_ms / 1000).toFixed(2)} s
- **Peak Memory**: ${formatBytes(r.bytes_allocated_peak)}
- **Cache Hit Rate**: ${cacheRate}
- **Frames Total**: ${r.frames_total}

## Core Render Phases
${corePhasesText || 'None'}

## Node Execution Bottlenecks
${nodePhasesText || 'None'}

## Telemetry Counters
${countersText}`;

    navigator.clipboard.writeText(text);
    setCopiedMetrics(true);
    setTimeout(() => setCopiedMetrics(false), 2000);
  };

  const formatBytes = (bytes) => {
    if (!bytes) return 'N/A';
    if (bytes >= 1024 * 1024 * 1024) {
      return `${(bytes / (1024 * 1024 * 1024)).toFixed(2)} GB`;
    }
    const mb = bytes / (1024 * 1024);
    return `${mb.toFixed(1)} MB`;
  };

  const formatIso = (isoStr) => {
    if (!isoStr) return '';
    const date = new Date(isoStr);
    return date.toLocaleString();
  };

  const formatCounterValue = (name, val) => {
    if (typeof val !== 'number') return val;
    if (name.includes('bytes') || name.includes('memory') || name.includes('peak')) {
      return formatBytes(val);
    }
    if (val >= 1000000) {
      return `${(val / 1000000).toFixed(2)}M`;
    }
    if (val >= 1000) {
      return `${(val / 1000).toFixed(1)}K`;
    }
    return val.toLocaleString();
  };

  // Color threshold helpers
  const getFpsColor = (fps) => {
    if (!fps) return 'var(--text-primary)';
    if (fps < 1.0) return 'var(--color-danger)';
    if (fps < 5.0) return 'var(--color-warning)';
    return 'var(--color-success)';
  };

  const getCacheHitColor = (rate) => {
    if (rate < 20.0) return 'var(--color-danger)';
    if (rate < 60.0) return 'var(--color-warning)';
    return 'var(--color-success)';
  };

  // Scaling helpers for selected frame
  const getScaledNodeDuration = (nodeDuration) => {
    if (!selectedFrame || !runDetail || !runDetail.run) return nodeDuration;
    const avgFrameDur = runDetail.run.wall_time_ms / runDetail.run.frames_total;
    const factor = selectedFrame.duration_ms / Math.max(avgFrameDur, 1);
    return nodeDuration * factor;
  };

  const getScaledCounterValue = (counterName, value) => {
    if (!selectedFrame || !runDetail || !runDetail.run) return value;
    if (typeof value !== 'number') return value;
    if (counterName === 'bytes_allocated_peak' || counterName.includes('memory') || counterName.includes('peak')) {
      return value;
    }
    return Math.round(value / runDetail.run.frames_total);
  };

  const renderInfoIcon = (key) => {
    const desc = INFO_DESCRIPTIONS[key] || INFO_DESCRIPTIONS[key.replace(' ', '_')] || INFO_DESCRIPTIONS[key.replace(/ /g, '_')];
    if (!desc) return null;
    return (
      <span className="info-icon" title={desc}>
        ⓘ
      </span>
    );
  };

  const renderPreviewPanel = () => {
    if (!runDetail || !runDetail.run) return null;

    const outputPath = runDetail.run.output_path || '';
    const artifactUrl = outputPathToArtifactUrl(outputPath);
    const canPreview = Boolean(outputPath) && (isVideoOutput(outputPath) || isImageOutput(outputPath));

    return (
      <section className="glass-panel preview-panel">
        <div className="panel-title">
          <span>Render Preview</span>
          <span className="preview-path">{outputPath || 'No output path'}</span>
        </div>
        {canPreview ? (
          isVideoOutput(outputPath) ? (
            <video className="render-preview-media" controls playsInline src={artifactUrl} />
          ) : (
            <img className="render-preview-media" src={artifactUrl} alt={runDetail.run.composition_id} />
          )
        ) : (
          <div className="preview-empty">
            No preview available for this output type.
          </div>
        )}
      </section>
    );
  };

  // Generate SVG elements for the frame chart
  const renderFrameChart = () => {
    if (!runDetail || !runDetail.frames || runDetail.frames.length === 0) {
      return <div className="text-muted">No frame data available for this run.</div>;
    }

    const frames = runDetail.frames;
    const maxDur = Math.max(...frames.map(f => f.duration_ms), 40); // at least 40ms to keep target lines scaled nicely
    const chartHeight = 220;
    const chartWidth = 800;
    const paddingLeft = 45;
    const paddingBottom = 25;
    const barWidth = Math.max(3, (chartWidth - paddingLeft) / frames.length - 2);

    // Target threshold lines
    const targets = [
      { ms: 16.67, label: '16.7ms (60 FPS)', color: 'rgba(16, 185, 129, 0.45)' },
      { ms: 33.33, label: '33.3ms (30 FPS)', color: 'rgba(245, 158, 11, 0.45)' }
    ];

    return (
      <div className="chart-container">
        <svg width="100%" height="100%" viewBox={`0 0 ${chartWidth} ${chartHeight}`} style={{ overflow: 'visible' }}>
          {/* Y Axis Grid lines */}
          {[0, 0.25, 0.5, 0.75, 1].map((ratio, idx) => {
            const y = chartHeight - paddingBottom - ratio * (chartHeight - paddingBottom - 15);
            const val = (ratio * maxDur).toFixed(0);
            return (
              <g key={idx}>
                <line x1={paddingLeft} y1={y} x2={chartWidth} y2={y} stroke="rgba(255,255,255,0.04)" />
                <text x={paddingLeft - 8} y={y + 3} fill="var(--text-muted)" fontSize="9" textAnchor="end" fontFamily="var(--font-mono)">
                  {val}ms
                </text>
              </g>
            );
          })}

          {/* Target guidelines */}
          {targets.map((t, idx) => {
            if (t.ms > maxDur) return null;
            const y = chartHeight - paddingBottom - (t.ms / maxDur) * (chartHeight - paddingBottom - 15);
            return (
              <g key={`target-${idx}`}>
                <line x1={paddingLeft} y1={y} x2={chartWidth} y2={y} stroke={t.color} strokeDasharray="3 3" strokeWidth="1.2" />
                <text x={chartWidth - 5} y={y - 4} fill={t.color} fontSize="8" textAnchor="end" fontFamily="var(--font-sans)" fontWeight="600">
                  {t.label}
                </text>
              </g>
            );
          })}

          {/* Render Frame Bars */}
          {frames.map((f, i) => {
            const x = paddingLeft + i * ((chartWidth - paddingLeft) / frames.length);
            const h = (f.duration_ms / maxDur) * (chartHeight - paddingBottom - 15);
            const y = chartHeight - paddingBottom - h;

            const isHit = f.cache_hit;
            const barColor = isHit ? 'var(--color-success)' : 'var(--color-primary)';
            const isHovered = hoveredFrame && hoveredFrame.frame_number === f.frame_number;
            const isSelected = selectedFrame && selectedFrame.frame_number === f.frame_number;

            return (
              <rect
                key={f.frame_number}
                x={x}
                y={y}
                width={barWidth}
                height={Math.max(2, h)}
                fill={isSelected ? 'var(--color-accent)' : (isHovered ? 'var(--color-info)' : barColor)}
                opacity={isSelected ? 1 : (isHovered ? 0.95 : 0.75)}
                rx="2"
                style={{ cursor: 'pointer', transition: 'all 0.12s ease' }}
                onMouseEnter={() => setHoveredFrame(f)}
                onMouseLeave={() => setHoveredFrame(null)}
                onClick={() => setSelectedFrame(isSelected ? null : f)}
              />
            );
          })}

          {/* X Axis line */}
          <line
            x1={paddingLeft}
            y1={chartHeight - paddingBottom}
            x2={chartWidth}
            y2={chartHeight - paddingBottom}
            stroke="var(--border-color)"
          />
          <text x={(chartWidth + paddingLeft) / 2} y={chartHeight - 4} fill="var(--text-muted)" fontSize="9" textAnchor="middle">
            Frames (0 to {frames.length - 1})
          </text>
        </svg>
      </div>
    );
  };

  const cacheRateNum = runDetail && runDetail.run.cache_hits + runDetail.run.cache_misses > 0 
    ? (runDetail.run.cache_hits / (runDetail.run.cache_hits + runDetail.run.cache_misses) * 100)
    : 0;
  const cacheRateStr = runDetail && runDetail.run.cache_hits + runDetail.run.cache_misses > 0 
    ? `${cacheRateNum.toFixed(1)}%`
    : '0.0%';

  return (
    <div className={`dashboard-container ${sidebarCollapsed ? 'sidebar-collapsed' : ''}`}>
      {/* Sidebar: Runs selection */}
      <aside className={`sidebar ${sidebarCollapsed ? 'collapsed' : ''}`}>
        <div className="sidebar-header">
          {!sidebarCollapsed && (
            <div className="brand">
              <span className="brand-logo">CHRONON3D</span>
              <span className="brand-badge">TELEMETRY</span>
            </div>
          )}
          <button className="collapse-btn" onClick={() => setSidebarCollapsed(!sidebarCollapsed)} title={sidebarCollapsed ? "Expand sidebar" : "Collapse sidebar"}>
            {sidebarCollapsed ? '▶' : '◀'}
          </button>
        </div>
        {!sidebarCollapsed && (
          <div className="runs-list">
            {runs.map((r) => (
              <div
                key={r.run_id}
                className={`run-card ${selectedRunId === r.run_id ? 'active' : ''}`}
                onClick={() => setSelectedRunId(r.run_id)}
              >
                <div className="run-card-header">
                  <span className="comp-name">{r.composition_id}</span>
                  <span className={`status-indicator ${r.success ? 'status-success' : 'status-failed'}`} />
                </div>
                <div className="run-meta">
                  <span>{r.frames_total} frames</span>
                  <span>{(r.wall_time_ms / 1000).toFixed(2)}s</span>
                </div>
              </div>
            ))}
          </div>
        )}
      </aside>

      {/* Main viewport */}
      <main className="main-content">
        {/* Toggle sidebar when completely collapsed */}
        {sidebarCollapsed && (
          <button className="expand-sidebar-btn" onClick={() => setSidebarCollapsed(false)} title="Expand sidebar">
            ☰ Menu
          </button>
        )}

        {error && <div className="glass-panel error-banner">{error}</div>}

        {loading && !runDetail && (
          <div className="loading-container">
            <div className="loading-spinner" />
            <p>Loading telemetry details...</p>
          </div>
        )}

        {runDetail && (
          <>
            <header className="content-header">
              <div>
                <h1 className="run-title">
                  {runDetail.run.composition_id}
                  <span className="run-id-badge">
                    {runDetail.run.run_id}
                  </span>
                </h1>
                <p style={{ color: 'var(--text-secondary)', fontSize: '0.9rem', marginTop: 4 }}>
                  Rendered at {formatIso(runDetail.run.finished_at_iso)}
                </p>
              </div>
              <div style={{ display: 'flex', gap: '12px', alignItems: 'center' }}>
                <span
                  style={{
                    padding: '6px 16px',
                    borderRadius: '20px',
                    fontSize: '0.85rem',
                    fontWeight: 600,
                    background: runDetail.run.success ? 'var(--color-success-glow)' : 'var(--color-danger-glow)',
                    color: runDetail.run.success ? 'var(--color-success)' : 'var(--color-danger)',
                    border: `1px solid ${runDetail.run.success ? 'var(--color-success)' : 'var(--color-danger)'}`
                  }}
                >
                  {runDetail.run.success ? 'SUCCESS' : 'FAILED'}
                </span>
                <button className="copy-btn copy-all-btn" onClick={copyMetricsToClipboard}>
                  {copiedMetrics ? '✓ Copied!' : '📋 Copy Metrics Report'}
                </button>
              </div>
            </header>

            {renderPreviewPanel()}

            {/* Metrics cards grid */}
            <section className="metrics-grid">
              <div className="glass-panel metric-card">
                <div className="metric-label">
                  Effective FPS
                  {renderInfoIcon('effective_fps')}
                </div>
                <div className="metric-value" style={{ color: getFpsColor(runDetail.run.effective_fps) }}>
                  {runDetail.run.effective_fps ? runDetail.run.effective_fps.toFixed(2) : '0.00'}
                  <span className="metric-unit">fps</span>
                </div>
              </div>
              <div className="glass-panel metric-card">
                <div className="metric-label">
                  Wall Duration
                  {renderInfoIcon('wall_time_ms')}
                </div>
                <div className="metric-value">
                  {(runDetail.run.wall_time_ms / 1000).toFixed(2)}
                  <span className="metric-unit">s</span>
                </div>
              </div>
              <div className="glass-panel metric-card">
                <div className="metric-label">
                  Peak Memory
                  {renderInfoIcon('bytes_allocated_peak')}
                </div>
                <div className="metric-value">
                  {formatBytes(runDetail.run.bytes_allocated_peak)}
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

            {/* Interactive timeline (full width) */}
            <div className="details-layout">
              <section className="glass-panel details-panel">
                <div className="panel-title">
                  <span>Frame Timeline (Click bar to select frame)</span>
                  {(hoveredFrame || selectedFrame) && (
                    <span className="timeline-selection-info">
                      {selectedFrame ? 'Selected' : 'Hovered'}: Frame #{(selectedFrame || hoveredFrame).frame_number} | Duration: {(selectedFrame || hoveredFrame).duration_ms.toFixed(1)}ms | Cache Hit: {(selectedFrame || hoveredFrame).cache_hit ? 'YES' : 'NO'} | Dirty Ratio: {((selectedFrame || hoveredFrame).dirty_area_ratio * 100).toFixed(1)}%
                    </span>
                  )}
                </div>
                {renderFrameChart()}
              </section>
            </div>

            {/* Interactive selection badge */}
            {selectedFrame && (
              <div className="filter-badge-row">
                <span className="filter-badge">
                  ⚡ Showing estimated metrics for Frame #{selectedFrame.frame_number}
                </span>
                <button className="clear-filter-btn" onClick={() => setSelectedFrame(null)}>
                  ✕ Reset to Run Averages
                </button>
              </div>
            )}

            {/* 3-column Phase Profiles & Telemetry Counters Section */}
            <section className="profile-grids">
              {/* Grid 1: Renderer Core Phases */}
              <div className="glass-panel profile-grid-card">
                <div className="panel-title">
                  <span>Core Render Phases</span>
                  {renderInfoIcon('rendering_loop')}
                  <button className="copy-btn" onClick={copyPhasesToClipboard} style={{ marginLeft: 'auto' }}>
                    {copiedPhases ? '✓' : '📋'}
                  </button>
                </div>
                <div className="phases-list">
                  {runDetail.phases
                    .filter(p => !p.phase_name.startsWith('node:'))
                    .map((p, idx) => {
                      const scaledTime = getScaledNodeDuration(p.duration_ms);
                      return (
                        <div key={idx} className="phase-item">
                          <span className="phase-name">
                            {p.phase_name}
                            {renderInfoIcon(p.phase_name)}
                          </span>
                          <span className="phase-time">
                            {scaledTime.toFixed(2)} ms
                            {selectedFrame && <span className="scaled-indicator">*</span>}
                          </span>
                        </div>
                      );
                    })}
                </div>
              </div>

              {/* Grid 2: Node Execution Bottlenecks */}
              <div className="glass-panel profile-grid-card">
                <div className="panel-title">
                  <span>Node Execution Bottlenecks</span>
                </div>
                <div className="phases-list">
                  {(() => {
                    const nodePhases = runDetail.phases.filter(p => p.phase_name.startsWith('node:'));
                    const maxNodeDur = Math.max(...nodePhases.map(p => getScaledNodeDuration(p.duration_ms)), 1);

                    return nodePhases.map((p, idx) => {
                      const scaledTime = getScaledNodeDuration(p.duration_ms);
                      const pct = (scaledTime / maxNodeDur) * 100;
                      const cleanName = p.phase_name.replace('node:', '');

                      return (
                        <div key={idx} className="phase-item relative-item">
                          <div className="sparkline-bg" style={{ width: `${pct}%` }} />
                          <span className="phase-name phase-name-node">
                            {cleanName}
                            {renderInfoIcon(cleanName)}
                          </span>
                          <span className="phase-time">
                            {scaledTime.toFixed(2)} ms
                            {selectedFrame && <span className="scaled-indicator">*</span>}
                          </span>
                        </div>
                      );
                    });
                  })()}
                  {runDetail.phases.filter(p => p.phase_name.startsWith('node:')).length === 0 && (
                    <div className="text-muted" style={{ padding: '10px 14px', fontSize: '0.85rem', textAlign: 'center' }}>
                      No node execution trace data. Use --benchmark-all to gather.
                    </div>
                  )}
                </div>
              </div>

              {/* Grid 3: Telemetry Counters */}
              <div className="glass-panel profile-grid-card">
                <div className="panel-title">
                  <span>Telemetry Counters</span>
                </div>
                <div className="phases-list">
                  {runDetail.counters && runDetail.counters.map((c, idx) => {
                    const cleanName = c.counter_name.replace(/_/g, ' ');
                    const scaledVal = getScaledCounterValue(c.counter_name, c.counter_value);
                    return (
                      <div key={idx} className="phase-item">
                        <span className="phase-name" style={{ color: 'var(--text-secondary)' }}>
                          {cleanName}
                          {renderInfoIcon(c.counter_name)}
                        </span>
                        <span className="phase-time" style={{ color: 'var(--color-info)' }}>
                          {formatCounterValue(c.counter_name, scaledVal)}
                          {selectedFrame && c.counter_name !== 'bytes_allocated_peak' && <span className="scaled-indicator">*</span>}
                        </span>
                      </div>
                    );
                  })}
                  {(!runDetail.counters || runDetail.counters.length === 0) && (
                    <div className="text-muted" style={{ padding: '10px 14px', fontSize: '0.85rem', textAlign: 'center' }}>
                      No telemetry counter data.
                    </div>
                  )}
                </div>
              </div>
            </section>
          </>
        )}
      </main>
    </div>
  );
}

export default App;
