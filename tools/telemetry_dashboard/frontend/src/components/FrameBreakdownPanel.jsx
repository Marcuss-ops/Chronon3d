import React from 'react';
import { renderInfoIcon } from '../utils/format.jsx';
import { getAggregatedLayers, getAggregatedNodes } from '../utils/aggregate.js';

function MetricChip({ label, value, tone = 'var(--color-accent)' }) {
  return (
    <div className="glass-panel" style={{ padding: '12px 14px', background: 'rgba(255,255,255,0.02)' }}>
      <div style={{ fontSize: '0.72rem', color: 'var(--text-muted)', marginBottom: '6px' }}>{label}</div>
      <div style={{ color: tone, fontSize: '1.05rem', fontWeight: 700 }}>{value}</div>
    </div>
  );
}

export default function FrameBreakdownPanel({ runDetail, frame }) {
  if (!runDetail || !runDetail.run || !frame) return null;

  const nodes = getAggregatedNodes(runDetail, frame).slice().sort((a, b) => b.duration_ms - a.duration_ms);
  const layers = getAggregatedLayers(runDetail, frame).slice().sort((a, b) => b.duration_ms - a.duration_ms);
  const runAvgMs = runDetail.run.frames_total > 0
    ? Number(runDetail.run.render_ms || runDetail.run.wall_time_ms || 0) / runDetail.run.frames_total
    : 0;
  const frameMs = Number(frame.duration_ms || 0);
  const graphEvalMs = Number(frame.graph_eval_ms || 0);
  const queueWaitMs = Number(frame.queue_wait_ms || 0);
  const conversionCopyMs = Number(frame.conversion_copy_ms || 0);
  const encoderMs = Number(frame.encoder_ms || 0);
  const pipeWriteMs = Number(frame.pipe_write_ms || 0);
  const nativeConvertMs = Number(frame.native_convert_ms || 0);
  const nativeSendMs = Number(frame.native_send_ms || 0);
  const nativeReceiveMs = Number(frame.native_receive_ms || 0);
  const nativeMuxMs = Number(frame.native_mux_ms || 0);
  const dirtyPct = Number(frame.dirty_area_ratio || 0) * 100;
  const cacheLabel = frame.cache_hit ? 'Cache hit' : 'Cache miss';
  const topNode = nodes[0];
  const topLayer = layers[0];
  const frameCountLabel = `${nodes.length.toLocaleString()} node events · ${layers.length.toLocaleString()} layer events`;
  const pipelineEstimateMs = graphEvalMs + queueWaitMs + conversionCopyMs + encoderMs;
  const hasNativeTelemetry = nativeConvertMs > 0 || nativeSendMs > 0 || nativeReceiveMs > 0 || nativeMuxMs > 0;
  const pipelineCandidates = [
    { label: 'Frame conversion / copy', value: conversionCopyMs },
    { label: 'Encoder / pipe', value: Math.max(encoderMs, pipeWriteMs, nativeSendMs, nativeReceiveMs, nativeMuxMs) },
    { label: 'Queue wait', value: queueWaitMs },
    { label: 'Graph eval', value: graphEvalMs },
  ].sort((a, b) => b.value - a.value);
  const topPipeline = pipelineCandidates[0];

  const likelyBottleneck = (() => {
    const nodeHint = topNode
      ? (topNode.node_type === 'Composite' ? 'Composite / blend cost'
        : topNode.node_type === 'Transform' ? 'Transform / matrix cost'
        : topNode.node_type === 'Source' ? 'Source / fetch cost'
        : topNode.node_type === 'Clear' ? 'Clear / framebuffer churn'
        : 'Mixed node work')
      : 'No node telemetry';

    if (!topPipeline || topPipeline.value <= 0) {
      return nodeHint;
    }

    if (topPipeline.value > (topNode ? topNode.duration_ms : 0)) {
      return topPipeline.label;
    }

    return nodeHint;
  })();

  return (
    <section className="glass-panel details-panel animate-fade-in" style={{ marginTop: '16px' }}>
      <div className="panel-title">
        <span>Frame Breakdown</span>
        <span className="timeline-selection-info">
          Frame #{frame.frame_number} · {frameMs.toFixed(1)} ms · {cacheLabel} · Dirty {dirtyPct.toFixed(1)}%
        </span>
      </div>
      <div className="filter-badge-row" style={{ marginTop: 0 }}>
        <span className="filter-badge">
          ⚡ {frameMs > runAvgMs * 1.5 && runAvgMs > 0 ? 'Spike frame' : 'Normal frame'} compared to render average
        </span>
        <span className="filter-badge">
          {frameCountLabel}
        </span>
      </div>

      <div className="metrics-grid" style={{ marginBottom: '16px' }}>
        <MetricChip label="Render Loop" value={`${frameMs.toFixed(2)} ms`} tone="var(--color-info)" />
        <MetricChip label="Graph Eval" value={`${graphEvalMs.toFixed(2)} ms`} tone="var(--color-accent)" />
        <MetricChip label="Queue Wait" value={`${queueWaitMs.toFixed(2)} ms`} tone="var(--color-warning)" />
        <MetricChip label="Dirty Coverage" value={`${dirtyPct.toFixed(1)}%`} tone="var(--color-warning)" />
        <MetricChip label="Cache" value={cacheLabel} tone={frame.cache_hit ? 'var(--color-success)' : 'var(--color-warning)'} />
        <MetricChip label="Pipeline Estimate" value={`${pipelineEstimateMs.toFixed(2)} ms`} tone="var(--color-primary)" />
      </div>

      <div className="metrics-grid" style={{ marginBottom: '16px' }}>
        <MetricChip
          label="Conversion Copy"
          value={`${conversionCopyMs.toFixed(2)} ms`}
          tone="var(--color-danger)"
        />
        <MetricChip
          label="Encoder"
          value={`${encoderMs.toFixed(2)} ms`}
          tone="var(--color-primary)"
        />
        <MetricChip
          label="Pipe Write"
          value={`${pipeWriteMs.toFixed(2)} ms`}
          tone="var(--color-accent)"
        />
        <MetricChip
          label="Native AV"
          value={hasNativeTelemetry
            ? `${nativeConvertMs.toFixed(2)} / ${nativeSendMs.toFixed(2)} / ${nativeReceiveMs.toFixed(2)} / ${nativeMuxMs.toFixed(2)} ms`
            : 'Not recorded'}
          tone="var(--color-primary)"
        />
        <MetricChip
          label="Likely Bottleneck"
          value={likelyBottleneck}
          tone="var(--color-danger)"
        />
      </div>

      <div className="filter-badge-row" style={{ marginTop: 0, marginBottom: '16px' }}>
        <span className="filter-badge">
          {topNode ? `Top node: ${topNode.node_name || '(unnamed)'} ${topNode.duration_ms.toFixed(2)} ms` : 'No node telemetry'}
        </span>
        <span className="filter-badge">
          {topLayer ? `Top layer: ${topLayer.layer_name || topLayer.layer_id} ${topLayer.duration_ms.toFixed(2)} ms` : 'No layer telemetry'}
        </span>
        <span className="filter-badge">
          {frameMs > runAvgMs * 1.5 && runAvgMs > 0 ? 'Spike needs drill-down' : 'Healthy frame pacing'}
        </span>
      </div>

      <div className="details-layout" style={{ marginTop: '0' }}>
        <section className="glass-panel details-panel" style={{ margin: 0 }}>
          <div className="panel-title">
            <span>Top Node Events</span>
            {renderInfoIcon('nodes_executed')}
          </div>
          <div className="results-container">
            <table className="results-table">
              <thead>
                <tr>
                  <th>Node</th>
                  <th>Type</th>
                  <th>Duration</th>
                  <th>Cache</th>
                  <th>Pixels</th>
                </tr>
              </thead>
              <tbody>
                {nodes.slice(0, 6).map((n, idx) => (
                  <tr key={`${n.node_name}-${n.node_type}-${idx}`}>
                    <td>
                      <div style={{ fontWeight: 600 }}>{n.node_name || '(unnamed)'}</div>
                      <div style={{ fontSize: '0.75rem', color: 'var(--text-muted)' }}>{n.layer_id || 'n/a'}</div>
                    </td>
                    <td>{n.node_type}</td>
                    <td style={{ color: 'var(--color-primary)', fontWeight: 600 }}>{n.duration_ms.toFixed(2)} ms</td>
                    <td>{n.cache_status}</td>
                    <td>{Number(n.pixels_touched || 0).toLocaleString()}</td>
                  </tr>
                ))}
                {nodes.length === 0 && (
                  <tr>
                    <td colSpan="5" style={{ textAlign: 'center', color: 'var(--text-muted)', padding: '18px' }}>
                      No node telemetry recorded for this frame.
                    </td>
                  </tr>
                )}
              </tbody>
            </table>
          </div>
        </section>

        <section className="glass-panel details-panel" style={{ margin: 0 }}>
          <div className="panel-title">
            <span>Top Layer Events</span>
            {renderInfoIcon('layers_rendered')}
          </div>
          <div className="results-container">
            <table className="results-table">
              <thead>
                <tr>
                  <th>Layer</th>
                  <th>Type</th>
                  <th>Duration</th>
                  <th>Visible</th>
                  <th>Pixels</th>
                </tr>
              </thead>
              <tbody>
                {layers.slice(0, 6).map((l, idx) => (
                  <tr key={`${l.layer_id || idx}`}>
                    <td>
                      <div style={{ fontWeight: 600 }}>{l.layer_name || 'Unnamed'}</div>
                      <div style={{ fontSize: '0.75rem', color: 'var(--text-muted)' }}>{l.layer_id || 'n/a'}</div>
                    </td>
                    <td>{l.layer_type || 'Unknown'}</td>
                    <td style={{ color: 'var(--color-accent)', fontWeight: 600 }}>{l.duration_ms.toFixed(2)} ms</td>
                    <td>{l.visible ? 'Yes' : 'No'}</td>
                    <td>{Number(l.visible_pixels || 0).toLocaleString()}</td>
                  </tr>
                ))}
                {layers.length === 0 && (
                  <tr>
                    <td colSpan="5" style={{ textAlign: 'center', color: 'var(--text-muted)', padding: '18px' }}>
                      No layer telemetry recorded for this frame.
                    </td>
                  </tr>
                )}
              </tbody>
            </table>
          </div>
        </section>
      </div>
    </section>
  );
}
