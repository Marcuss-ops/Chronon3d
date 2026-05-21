import React, { useState } from 'react';
import { renderInfoIcon, formatCounterValue } from '../utils/format.jsx';
import { copyTextToClipboard } from '../utils/clipboard.js';
import { getAggregatedNodes } from '../utils/aggregate.js';

function CopyBtn({ onClick, label, copiedLabel }) {
  const [copied, setCopied] = useState(false);
  const handleClick = () => {
    onClick();
    setCopied(true);
    setTimeout(() => setCopied(false), 2000);
  };
  return (
    <button className="copy-btn" onClick={handleClick} style={{ marginLeft: 'auto' }}>
      {copied ? (copiedLabel || '✓') : (label || '📋')}
    </button>
  );
}

export default function ProfilePanels({ runDetail, selectedFrame }) {
  if (!runDetail || !runDetail.run) return null;

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

  const aggregatedNodes = getAggregatedNodes(runDetail, selectedFrame)
    .slice()
    .sort((a, b) => b.duration_ms - a.duration_ms);
  const fallbackNodePhases = (runDetail.phases || [])
    .filter(p => p.phase_name.startsWith('node:'))
    .slice()
    .sort((a, b) => b.duration_ms - a.duration_ms)
    .map(p => ({
      node_name: p.phase_name.replace('node:', ''),
      node_type: 'phase',
      duration_ms: p.duration_ms,
      executions: 1,
    }));
  const nodeRows = aggregatedNodes.length > 0 ? aggregatedNodes : fallbackNodePhases;

  const copyProfileText = async () => {
    const phasesText = (runDetail.phases || [])
      .filter(p => !p.phase_name.startsWith('node:'))
      .map(p => `${p.phase_name}: ${p.duration_ms.toFixed(2)} ms`);

    const nodeText = nodeRows.map(n => {
      const suffix = selectedFrame ? `Frame #${selectedFrame.frame_number}` : `${n.executions}x`;
      return `${n.node_name} (${n.node_type}) - ${n.duration_ms.toFixed(2)} ms [${suffix}]`;
    });

    const text = [...phasesText, '', ...nodeText].join('\n');
    await copyTextToClipboard(text);
  };

  return (
    <section className="profile-layout">
      <div className="profile-top-row">
        {/* Grid 1: Renderer Core Phases */}
        <div className="glass-panel profile-grid-card">
          <div className="panel-title">
            <span>Core Render Phases</span>
            {renderInfoIcon('rendering_loop')}
            <CopyBtn onClick={copyProfileText} label="📋" copiedLabel="✓" />
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
              const maxNodeDur = Math.max(...nodeRows.map(n => getScaledNodeDuration(n.duration_ms)), 1);

              return nodeRows.map((n, idx) => {
                const scaledTime = getScaledNodeDuration(n.duration_ms);
                const pct = (scaledTime / maxNodeDur) * 100;
                const cleanName = n.node_name;

                return (
                  <div key={`${n.node_name}-${n.node_type}-${idx}`} className="phase-item relative-item">
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
            {nodeRows.length === 0 && (
              <div className="text-muted" style={{ padding: '10px 14px', fontSize: '0.85rem', textAlign: 'center' }}>
                No node telemetry events recorded for this run.
              </div>
            )}
          </div>
        </div>
      </div>

      {/* Grid 3: Telemetry Counters (Full Width Below) */}
      <div className="glass-panel profile-grid-card profile-bottom-panel">
        <div className="panel-title">
          <span>Telemetry Counters</span>
        </div>
        <div className="counters-horizontal-list">
          {runDetail.counters && runDetail.counters.map((c, idx) => {
            const cleanName = c.counter_name.replace(/_/g, ' ');
            const scaledVal = getScaledCounterValue(c.counter_name, c.counter_value);
            return (
              <div key={idx} className="phase-item counter-item-horizontal">
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
            <div className="text-muted" style={{ padding: '10px 14px', fontSize: '0.85rem', textAlign: 'center', width: '100%' }}>
              No telemetry counter data.
            </div>
          )}
        </div>
      </div>
    </section>
  );

}
