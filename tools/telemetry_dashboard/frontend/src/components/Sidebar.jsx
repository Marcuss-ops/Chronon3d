import React, { useState } from 'react';

export default function Sidebar({ 
  runs, 
  selectedRunId, 
  onSelectRun, 
  comparisonRunId,
  onSelectComparisonRun,
  collapsed, 
  onToggleCollapse 
}) {
  const [query, setQuery] = useState('');

  const filteredRuns = runs.filter(r => 
    r.composition_id.toLowerCase().includes(query.toLowerCase()) ||
    (r.git_commit_short && r.git_commit_short.toLowerCase().includes(query.toLowerCase()))
  );

  return (
    <aside className={`sidebar ${collapsed ? 'collapsed' : ''}`}>
      <div className="sidebar-header">
        {!collapsed && (
          <div className="brand">
            <span className="brand-logo">CHRONON3D</span>
            <span className="brand-badge">TELEMETRY</span>
          </div>
        )}
        <button className="collapse-btn" onClick={onToggleCollapse} title={collapsed ? 'Expand sidebar' : 'Collapse sidebar'}>
          {collapsed ? '▶' : '◀'}
        </button>
      </div>
      {!collapsed && (
        <>
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
                {!collapsed && (
                   <button 
                    className={`compare-small-btn ${comparisonRunId === r.run_id ? 'active' : ''}`}
                    onClick={(e) => {
                      e.stopPropagation();
                      onSelectComparisonRun(comparisonRunId === r.run_id ? '' : r.run_id);
                    }}
                  >
                    {comparisonRunId === r.run_id ? 'Remove Comp' : 'Compare'}
                  </button>
                )}
              </div>
            ))}
          </div>
        </>
      )}
    </aside>
  );
}
