import React, { useState } from 'react';
import { renderInfoIcon, formatCounterValue } from '../utils/format.jsx';

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

  const copyPhasesText = () => {
    if (!runDetail.phases) return;
    const text = runDetail.phases.map(p => {
      const isNode = p.phase_name.startsWith('node:');
      const name = isNode ? p.phase_name.substring(5) : p.phase_name;
      return `${name}: ${p.duration_ms.toFixed(2)} ms`;
    }).join('\n');
    navigator.clipboard.writeText(text);
  };

  return (
    <section className="profile-layout">
      <div className="profile-top-row">
        {/* Grid 1: Renderer Core Phases */}
        <div className="glass-panel profile-grid-card">
          <div className="panel-title">
            <span>Core Render Phases</span>
            {renderInfoIcon('rendering_loop')}
            <CopyBtn onClick={copyPhasesText} label="📋" copiedLabel="✓" />
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
