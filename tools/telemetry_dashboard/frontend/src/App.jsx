import React, { useState, useEffect, useRef, useCallback } from 'react';
import './App.css';

import { fetchRuns, fetchRunDetail } from './api/telemetryApi.js';
import { formatBytes, formatIso, formatCounterValue } from './utils/format.jsx';

import Sidebar from './components/Sidebar.jsx';
import TabNavigation from './components/TabNavigation.jsx';
import PreviewPanel from './components/PreviewPanel.jsx';
import MetricsGrid from './components/MetricsGrid.jsx';
import FrameChart from './components/FrameChart.jsx';
import ProfilePanels from './components/ProfilePanels.jsx';
import LayersTable from './components/LayersTable.jsx';
import NodesTable from './components/NodesTable.jsx';

function App() {
  const [runs, setRuns] = useState([]);
  const [selectedRunId, setSelectedRunId] = useState('');
  const [runDetail, setRunDetail] = useState(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState('');

  const selectedRunIdRef = useRef(selectedRunId);
  useEffect(() => {
    selectedRunIdRef.current = selectedRunId;
  }, [selectedRunId]);

  const runsRef = useRef(runs);
  useEffect(() => {
    runsRef.current = runs;
  }, [runs]);

  const [sidebarCollapsed, setSidebarCollapsed] = useState(false);
  const [activeTab, setActiveTab] = useState('overview');

  const [selectedFrame, setSelectedFrame] = useState(null);
  const [hoveredFrame, setHoveredFrame] = useState(null);

  const [copiedMetrics, setCopiedMetrics] = useState(false);

  useEffect(() => {
    setSelectedFrame(null);
  }, [selectedRunId]);

  const loadRuns = useCallback(async () => {
    try {
      setLoading(true);
      const data = await fetchRuns();
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
  }, []);

  const loadRunDetail = useCallback(async (id, silent = false) => {
    try {
      if (!silent) setLoading(true);
      const data = await fetchRunDetail(id);
      setRunDetail(data);
      if (!silent) setError('');
    } catch (err) {
      if (!silent) setError(err.message);
    } finally {
      if (!silent) setLoading(false);
    }
  }, []);

  useEffect(() => {
    loadRuns();
    const interval = setInterval(async () => {
      try {
        const data = await fetchRuns();
        const prevRuns = runsRef.current;

        let newSelectedId = selectedRunIdRef.current;
        if (data.length > 0 && prevRuns.length > 0) {
          const isNewRun = !prevRuns.some(r => r.run_id === data[0].run_id);
          if (isNewRun) {
            newSelectedId = data[0].run_id;
          }
        } else if (data.length > 0 && !selectedRunIdRef.current) {
          newSelectedId = data[0].run_id;
        }

        setRuns(data);
        if (newSelectedId) {
          setSelectedRunId(newSelectedId);
          loadRunDetail(newSelectedId, true);
        }
      } catch (err) {
        console.error('Polling error:', err);
      }
    }, 3000);
    return () => clearInterval(interval);
  }, [loadRuns, loadRunDetail]);

  useEffect(() => {
    if (selectedRunId) {
      loadRunDetail(selectedRunId);
    }
  }, [selectedRunId, loadRunDetail]);

  const copyMetricsToClipboard = () => {
    if (!runDetail || !runDetail.run) return;
    const r = runDetail.run;
    const cacheRate = r.cache_hits + r.cache_misses > 0
      ? `${(r.cache_hits / (r.cache_hits + r.cache_misses) * 100).toFixed(1)}%`
      : '0.0%';

    const corePhasesText = runDetail.phases
      .filter(p => !p.phase_name.startsWith('node:'))
      .map(p => `- **${p.phase_name}**: ${p.duration_ms.toFixed(2)} ms`)
      .join('\n');

    const nodePhasesText = runDetail.phases
      .filter(p => p.phase_name.startsWith('node:'))
      .map(p => `- **${p.phase_name.replace('node:', '')}**: ${p.duration_ms.toFixed(2)} ms`)
      .join('\n');

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

  return (
    <div className={`dashboard-container ${sidebarCollapsed ? 'sidebar-collapsed' : ''}`}>
      <Sidebar
        runs={runs}
        selectedRunId={selectedRunId}
        onSelectRun={setSelectedRunId}
        collapsed={sidebarCollapsed}
        onToggleCollapse={() => setSidebarCollapsed(!sidebarCollapsed)}
      />

      <main className="main-content">
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
                  {copiedMetrics ? '\u2713 Copied!' : '\ud83d\udccb Copy Metrics Report'}
                </button>
              </div>
            </header>

            <TabNavigation
              activeTab={activeTab}
              onTabChange={setActiveTab}
              layerCount={runDetail.layer_events ? runDetail.layer_events.length : 0}
              nodeCount={runDetail.node_events ? runDetail.node_events.length : 0}
            />

            {activeTab === 'overview' && (
              <>
                <PreviewPanel run={runDetail.run} />
                <MetricsGrid runDetail={runDetail} />

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
                    <FrameChart
                      frames={runDetail.frames}
                      selectedFrame={selectedFrame}
                      hoveredFrame={hoveredFrame}
                      onSelectFrame={(f) => setSelectedFrame(prev => (prev && prev.frame_number === f?.frame_number) ? null : f)}
                      onHoverFrame={setHoveredFrame}
                    />
                  </section>
                </div>

                {selectedFrame && (
                  <div className="filter-badge-row">
                    <span className="filter-badge">
                      \u26a1 Showing estimated metrics for Frame #{selectedFrame.frame_number}
                    </span>
                    <button className="clear-filter-btn" onClick={() => setSelectedFrame(null)}>
                      \u2715 Reset to Run Averages
                    </button>
                  </div>
                )}

                <ProfilePanels
                  runDetail={runDetail}
                  selectedFrame={selectedFrame}
                />
              </>
            )}

            {activeTab === 'layers' && (
              <LayersTable
                runDetail={runDetail}
                selectedFrame={selectedFrame}
                onResetFrame={() => setSelectedFrame(null)}
              />
            )}

            {activeTab === 'nodes' && (
              <NodesTable
                runDetail={runDetail}
                selectedFrame={selectedFrame}
                onResetFrame={() => setSelectedFrame(null)}
              />
            )}


          </>
        )}
      </main>
    </div>
  );
}

export default App;
