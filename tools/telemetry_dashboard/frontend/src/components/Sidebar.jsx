import React, { useState, useCallback, useRef, useEffect } from 'react';

export default function Sidebar({
  runs,
  selectedRunId,
  onSelectRun,
  comparisonRunId,
  onSelectComparisonRun,
}) {
  const [isVisible, setIsVisible] = useState(false);
  const [isPinned, setIsPinned] = useState(false);
  const [query, setQuery] = useState('');
  const hideTimer = useRef(null);

  const togglePin = useCallback(() => {
    setIsPinned(prev => !prev);
  }, []);

  const showSidebar = useCallback(() => {
    if (hideTimer.current) {
      clearTimeout(hideTimer.current);
      hideTimer.current = null;
    }
    setIsVisible(true);
  }, []);

  const scheduleHide = useCallback(() => {
    if (isPinned) return;
    hideTimer.current = setTimeout(() => {
      setIsVisible(false);
    }, 300);
  }, [isPinned]);

  const toggleVisible = useCallback(() => {
    setIsVisible(prev => !prev);
  }, []);

  // Cleanup hide timer on unmount
  useEffect(() => {
    return () => { if (hideTimer.current) clearTimeout(hideTimer.current); };
  }, []);

  const runsArray = Array.isArray(runs) ? runs : [];
  const filteredRuns = runsArray.filter(r =>
    (r.composition_id || '').toLowerCase().includes(query.toLowerCase()) ||
    (r.git_commit_short && r.git_commit_short.toLowerCase().includes(query.toLowerCase()))
  );

  return (
    <>
      {/* Hover trigger strip on the left edge */}
      <div
        className="sidebar-peek-trigger"
        onMouseEnter={showSidebar}
        title="Hover to open sidebar"
      />

      {/* ── Sidebar ── */}
      <aside
        className={`sidebar ${isVisible ? 'visible' : 'hidden'}`}
        onMouseEnter={showSidebar}
        onMouseLeave={scheduleHide}
      >
        <div className="sidebar-header">
          <div className="brand">
            <span className="brand-logo">CHRONON3D</span>
            <span className="brand-badge">TELEMETRY</span>
          </div>
          <div style={{ display: 'flex', gap: '6px' }}>
            <button
              className={`pin-btn ${isPinned ? 'pinned' : ''}`}
              onClick={togglePin}
              title={isPinned ? 'Unpin sidebar' : 'Pin sidebar'}
            >
              {isPinned ? '📌' : '📍'}
            </button>
            <button
              className="pin-btn"
              onClick={toggleVisible}
              title="Close sidebar"
            >
              ✕
            </button>
          </div>
        </div>

        <div className="sidebar-search">
          <input
            type="text"
            placeholder={`Filter ${runsArray.length} runs by comp or commit...`}
            value={query}
            onChange={(e) => setQuery(e.target.value)}
            className="search-input"
          />
        </div>

        <div className="runs-list">
          {filteredRuns.length === 0 && (
            <div style={{ color: 'var(--text-muted)', fontSize: '0.85rem', textAlign: 'center', padding: '20px 0' }}>
              No runs match your filter
            </div>
          )}
          {filteredRuns.map((r) => (
            <div
              key={r.run_id}
              className={`run-card ${selectedRunId === r.run_id ? 'active' : ''} ${comparisonRunId === r.run_id ? 'comparing' : ''}`}
              onClick={() => onSelectRun(r.run_id)}
            >
              <div className="run-card-header">
                <span className="comp-name">{r.composition_id}</span>
                <div style={{ display: 'flex', gap: '4px' }}>
                  {selectedRunId === r.run_id && <span className="label-badge primary">Base</span>}
                  {comparisonRunId === r.run_id && <span className="label-badge secondary">Comp</span>}
                  <span className={`status-indicator ${r.success ? 'status-success' : 'status-failed'}`} />
                </div>
              </div>
              <div className="run-meta">
                <span>{r.frames_total} frames</span>
                <span style={{ fontSize: '0.75rem', opacity: 0.7 }}>{r.git_commit_short}</span>
              </div>
               <button
                  className={`compare-small-btn ${comparisonRunId === r.run_id ? 'active' : ''}`}
                  onClick={(e) => {
                    e.stopPropagation();
                    onSelectComparisonRun(comparisonRunId === r.run_id ? '' : r.run_id);
                  }}
                >
                  {comparisonRunId === r.run_id ? 'Remove Comp' : 'Compare'}
                </button>
            </div>
          ))}
        </div>

        {/* Status hint */}
        <div className="sidebar-auto-hint" onClick={togglePin}>
          {isPinned
            ? `📌 Pinned — ${filteredRuns.length} run${filteredRuns.length !== 1 ? 's' : ''} shown`
            : `⏎ ${filteredRuns.length} run${filteredRuns.length !== 1 ? 's' : ''} shown`
          }
        </div>
      </aside>
    </>
  );
}
