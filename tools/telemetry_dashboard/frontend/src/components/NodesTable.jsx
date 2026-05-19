import React from 'react';
import { getAggregatedNodes } from '../utils/aggregate.js';
import { formatBytes } from '../utils/format.jsx';

export default function NodesTable({ runDetail, selectedFrame, onResetFrame }) {
  const nodes = getAggregatedNodes(runDetail, selectedFrame);

  return (
    <section className="glass-panel details-panel animate-fade-in">
      <div className="panel-title">
        <span>🌳 Render Graph Node Execution Analysis</span>
        {selectedFrame ? (
          <span className="timeline-selection-info">Filtering Frame #{selectedFrame.frame_number}</span>
        ) : (
          <span className="timeline-selection-info">Averaged over all {runDetail?.run?.frames_total || 0} frames</span>
        )}
      </div>
      {selectedFrame && (
        <div className="filter-badge-row" style={{ marginTop: 0 }}>
          <span className="filter-badge">⚡ Showing exact metrics for Frame #{selectedFrame.frame_number}</span>
          <button className="clear-filter-btn" onClick={onResetFrame}>✕ Reset to Run Averages</button>
        </div>
      )}
      <div className="results-container">
        <table className="results-table">
          <thead>
            <tr>
              <th>Node Name / Type</th>
              <th>Executions</th>
              <th>Duration</th>
              <th>Cache Status</th>
              <th>Output Size</th>
              <th>Pixels Touched</th>
              <th>Cleared</th>
              <th>Composited</th>
              <th>Transformed</th>
              <th>Blurred</th>
            </tr>
          </thead>
          <tbody>
            {nodes.map((n, i) => (
              <tr key={`${n.node_name}-${n.node_type}-${i}`}>
                <td>
                  <div style={{ fontWeight: 600, color: 'var(--text-primary)' }}>{n.node_name}</div>
                  <div style={{ fontSize: '0.75rem', color: 'var(--text-muted)', fontFamily: 'var(--font-mono)' }}>{n.node_type}</div>
                </td>
                <td style={{ fontFamily: 'var(--font-mono)' }}>
                  {selectedFrame ? `Frame #${selectedFrame.frame_number}` : `${n.executions}x`}
                </td>
                <td style={{ color: 'var(--color-primary)', fontWeight: 600 }}>
                  {n.duration_ms.toFixed(2)} ms
                </td>
                <td>
                  <span className={`cache-badge cache-badge-${n.cache_status}`}>
                    {n.cache_status === 'hit' ? 'Hit' : (n.cache_status === 'miss' ? 'Miss' : (n.cache_status === 'bypass' ? 'Bypass' : `Hit Rate: ${(n.hit_rate * 100).toFixed(0)}%`))}
                  </span>
                </td>
                <td style={{ fontSize: '0.8rem', color: 'var(--text-muted)' }}>
                  {n.output_width}x{n.output_height}
                  <div style={{ fontSize: '0.7rem' }}>{formatBytes(n.output_bytes)}</div>
                </td>
                <td>{n.pixels_touched.toLocaleString()}</td>
                <td>{n.pixels_cleared.toLocaleString()}</td>
                <td>{n.pixels_composited.toLocaleString()}</td>
                <td>{n.pixels_transformed.toLocaleString()}</td>
                <td>{n.pixels_blurred.toLocaleString()}</td>
              </tr>
            ))}
            {nodes.length === 0 && (
              <tr>
                <td colSpan="10" style={{ textAlign: 'center', padding: '24px', color: 'var(--text-muted)' }}>
                  No node telemetry events recorded for this run.
                </td>
              </tr>
            )}
          </tbody>
        </table>
      </div>
    </section>
  );
}
