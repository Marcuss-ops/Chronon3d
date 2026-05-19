import React from 'react';

const chartHeight = 220;
const chartWidth = 800;
const paddingLeft = 45;
const paddingBottom = 25;

const targets = [
  { ms: 16.67, label: '16.7ms (60 FPS)', color: 'rgba(16, 185, 129, 0.45)' },
  { ms: 33.33, label: '33.3ms (30 FPS)', color: 'rgba(245, 158, 11, 0.45)' }
];

export default function FrameChart({ frames, selectedFrame, onSelectFrame, hoveredFrame, onHoverFrame }) {
  if (!frames || frames.length === 0) {
    return <div className="text-muted">No frame data available for this run.</div>;
  }

  const maxDur = Math.max(...frames.map(f => f.duration_ms), 40);
  const barWidth = Math.max(3, (chartWidth - paddingLeft) / frames.length - 2);
  const plotHeight = chartHeight - paddingBottom - 15;

  return (
    <div className="chart-container">
      <svg width="100%" height="100%" viewBox={`0 0 ${chartWidth} ${chartHeight}`} style={{ overflow: 'visible' }}>
        {/* Y Axis Grid lines */}
        {[0, 0.25, 0.5, 0.75, 1].map((ratio, idx) => {
          const y = chartHeight - paddingBottom - ratio * plotHeight;
          const val = (ratio * maxDur).toFixed(0);
          return (
            <g key={idx}>
              <line x1={paddingLeft} y1={y} x2={chartWidth} y2={y} stroke="rgba(255,255,255,0.04)" />
              <text x={paddingLeft - 8} y={y + 3} fill="var(--text-muted)" fontSize="9" textAnchor="end" fontFamily="var(--font-mono)">
                {val}ms
              </text>
            </g>
          );
        })}

        {/* Target guidelines */}
        {targets.map((t, idx) => {
          if (t.ms > maxDur) return null;
          const y = chartHeight - paddingBottom - (t.ms / maxDur) * plotHeight;
          return (
            <g key={`target-${idx}`}>
              <line x1={paddingLeft} y1={y} x2={chartWidth} y2={y} stroke={t.color} strokeDasharray="3 3" strokeWidth="1.2" />
              <text x={chartWidth - 5} y={y - 4} fill={t.color} fontSize="8" textAnchor="end" fontFamily="var(--font-sans)" fontWeight="600">
                {t.label}
              </text>
            </g>
          );
        })}

        {/* Render Frame Bars */}
        {frames.map((f, i) => {
          const x = paddingLeft + i * ((chartWidth - paddingLeft) / frames.length);
          const h = (f.duration_ms / maxDur) * plotHeight;
          const y = chartHeight - paddingBottom - h;

          const isHit = f.cache_hit;
          const barColor = isHit ? 'var(--color-success)' : 'var(--color-primary)';
          const isHovered = hoveredFrame && hoveredFrame.frame_number === f.frame_number;
          const isSelected = selectedFrame && selectedFrame.frame_number === f.frame_number;

          return (
            <rect
              key={`frame-${f.frame_number || i}`}
              x={x}
              y={y}
              width={barWidth}
              height={Math.max(2, h)}
              fill={isSelected ? 'var(--color-accent)' : (isHovered ? 'var(--color-info)' : barColor)}
              opacity={isSelected ? 1 : (isHovered ? 0.95 : 0.75)}
              rx="2"
              style={{ cursor: 'pointer', transition: 'all 0.12s ease' }}
              onMouseEnter={() => onHoverFrame(f)}
              onMouseLeave={() => onHoverFrame(null)}
              onClick={() => onSelectFrame(isSelected ? null : f)}
            />
          );
        })}

        {/* X Axis line */}
        <line
          x1={paddingLeft}
          y1={chartHeight - paddingBottom}
          x2={chartWidth}
          y2={chartHeight - paddingBottom}
          stroke="var(--border-color)"
        />
        <text x={(chartWidth + paddingLeft) / 2} y={chartHeight - 4} fill="var(--text-muted)" fontSize="9" textAnchor="middle">
          Frames (0 to {frames.length - 1})
        </text>
      </svg>
    </div>
  );
}
