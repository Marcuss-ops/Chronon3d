import React from 'react';
import { getAggregatedLayers } from '../utils/aggregate.js';

export default function LayersTable({ runDetail, selectedFrame, onResetFrame }) {
  const layers = getAggregatedLayers(runDetail, selectedFrame);

  return (
    <section className="glass-panel details-panel animate-fade-in">
      <div className="panel-title">
        <span>🥞 Layer-by-Layer Render Performance</span>
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
              <th>Layer ID / Name</th>
              <th>Type</th>
              <th>Avg Duration</th>
              <th>Visibility</th>
              <th>Culling Reason</th>
              <th>BBox ({selectedFrame ? 'Exact' : 'Avg Area'})</th>
              <th>Glyphs</th>
              <th>Images</th>
              <th>Effects</th>
            </tr>
          </thead>
          <tbody>
            {layers.map((l, i) => (
              <tr key={l.layer_id || i}>
                <td>
                  <div style={{ fontWeight: 600, color: 'var(--text-primary)' }}>{l.layer_name || 'Unnamed'}</div>
                  <div style={{ fontSize: '0.75rem', color: 'var(--text-muted)', fontFamily: 'var(--font-mono)' }}>{l.layer_id}</div>
                </td>
                <td>
                  <span className="type-badge layer-type-badge">{l.layer_type || 'Unknown'}</span>
                </td>
                <td style={{ color: 'var(--color-accent)', fontWeight: 600 }}>
                  {l.duration_ms.toFixed(2)} ms
                </td>
                <td>
                  {selectedFrame ? (
                    l.visible ? (
                      <span className="status-badge status-badge-success">Visible</span>
                    ) : (
                      <span className="status-badge status-badge-danger">Culled</span>
                    )
                  ) : (
                    <span className={l.cull_rate === 1 ? 'status-badge status-badge-danger' : (l.cull_rate > 0 ? 'status-badge status-badge-warning' : 'status-badge status-badge-success')}>
                      {l.cull_rate > 0 ? `${(l.cull_rate * 100).toFixed(0)}% Culled` : 'Always Visible'}
                    </span>
                  )}
                </td>
                <td style={{ fontSize: '0.8rem', color: 'var(--text-secondary)' }}>
                  {l.cull_reason || '-'}
                </td>
                <td style={{ fontSize: '0.8rem', color: 'var(--text-muted)' }}>
                  {selectedFrame ? (
                    `${l.bbox_w ? Math.round(l.bbox_w) : 0}x${l.bbox_h ? Math.round(l.bbox_h) : 0}`
                  ) : (
                    `${Math.round(l.area_pixels).toLocaleString()} px²`
                  )}
                </td>
                <td>{l.glyphs_rasterized || 0}</td>
                <td>{l.images_sampled || 0}</td>
                <td style={{ fontSize: '0.8rem', color: 'var(--text-secondary)' }}>
                  {l.effects || '-'}
                </td>
              </tr>
            ))}
            {layers.length === 0 && (
              <tr>
                <td colSpan="9" style={{ textAlign: 'center', padding: '24px', color: 'var(--text-muted)' }}>
                  No layer telemetry events recorded for this run.
                </td>
              </tr>
            )}
          </tbody>
        </table>
      </div>
    </section>
  );
}
