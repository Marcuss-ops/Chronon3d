import React from 'react';

export default function Sidebar({ runs, selectedRunId, onSelectRun, collapsed, onToggleCollapse }) {
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
        <div className="runs-list">
          {runs.map((r) => (
            <div
              key={r.run_id}
              className={`run-card ${selectedRunId === r.run_id ? 'active' : ''}`}
              onClick={() => onSelectRun(r.run_id)}
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
  );
}
