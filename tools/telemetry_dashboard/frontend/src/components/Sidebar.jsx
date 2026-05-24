import React, { useState, useRef, useEffect, useCallback } from 'react';

const AUTO_HIDE_DELAY_MS = 2500; // 2.5s senza hover → auto-hide

export default function Sidebar({ 
  runs, 
  selectedRunId, 
  onSelectRun, 
  comparisonRunId,
  onSelectComparisonRun,
}) {
  const [isVisible, setIsVisible] = useState(true);
  const [isPinned, setIsPinned] = useState(false);
  const hideTimerRef = useRef(null);
  const [query, setQuery] = useState('');

  // ── Auto-hide timer ────────────────────────────────────────────────
  const scheduleHide = useCallback(() => {
    if (isPinned) return; // pinned → no auto-hide
    if (hideTimerRef.current) clearTimeout(hideTimerRef.current);
    hideTimerRef.current = setTimeout(() => {
      setIsVisible(false);
    }, AUTO_HIDE_DELAY_MS);
  }, [isPinned]);

  const cancelHide = useCallback(() => {
    if (hideTimerRef.current) {
      clearTimeout(hideTimerRef.current);
      hideTimerRef.current = null;
    }
  }, []);

  // Mouse entra → espandi e cancella timer
  const handleSidebarEnter = useCallback(() => {
    cancelHide();
    setIsVisible(true);
  }, [cancelHide]);

  // Mouse esce → avvia timer auto-hide (se non pinnato)
  const handleSidebarLeave = useCallback(() => {
    scheduleHide();
  }, [scheduleHide]);

  // Mouse entra nel peek trigger → espandi
  const handlePeekEnter = useCallback(() => {
    cancelHide();
    setIsVisible(true);
  }, [cancelHide]);

  // Toggle pin
  const togglePin = useCallback(() => {
    setIsPinned(prev => {
      const next = !prev;
      if (next) {
        cancelHide();
        setIsVisible(true);
      } else {
        scheduleHide();
      }
      return next;
    });
  }, [cancelHide, scheduleHide]);

  // ── Start auto-hide timer on mount; cleanup on unmount ──
  useEffect(() => {
    scheduleHide();
    return () => {
      if (hideTimerRef.current) clearTimeout(hideTimerRef.current);
    };
  }, [scheduleHide]);

  const runsArray = Array.isArray(runs) ? runs : [];
  const filteredRuns = runsArray.filter(r => 
    (r.composition_id || '').toLowerCase().includes(query.toLowerCase()) ||
    (r.git_commit_short && r.git_commit_short.toLowerCase().includes(query.toLowerCase()))
  );

  return (
    <>
      {/* ── Peek trigger strip (sempre visibile sul bordo sinistro) ── */}
      <div
        className="sidebar-peek-trigger"
        onMouseEnter={handlePeekEnter}
        title="Hover per aprire sidebar"
      />

      {/* ── Sidebar ── */}
      <aside
        className={`sidebar ${isVisible ? 'visible' : 'hidden'}`}
        onMouseEnter={handleSidebarEnter}
        onMouseLeave={handleSidebarLeave}
      >
        <div className="sidebar-header">
          <div className="brand">
            <span className="brand-logo">CHRONON3D</span>
            <span className="brand-badge">TELEMETRY</span>
          </div>
          <button
            className={`pin-btn ${isPinned ? 'pinned' : ''}`}
            onClick={togglePin}
            title={isPinned ? 'Unpin sidebar (auto-hide riattivo)' : 'Pin sidebar (sempre aperta)'}
          >
            {isPinned ? '📌' : '📍'}
          </button>
        </div>

        <div className="sidebar-search">
          <input 
            type="text" 
            placeholder="Filter by comp or commit..." 
            value={query}
            onChange={(e) => setQuery(e.target.value)}
            className="search-input"
          />
        </div>

        <div className="runs-list">
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

        {/* Piccolo hint visivo quando la sidebar è visibile */}
        <div className="sidebar-auto-hint" onClick={togglePin}>
          {isPinned
            ? '📌 Pinnata — non si chiude' 
            : '⏎ Auto-hide: esci per chiudere'
          }
        </div>
      </aside>
    </>
  );
}
