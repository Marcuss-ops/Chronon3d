import React, { useEffect, useState } from 'react';
import Chart from 'react-apexcharts';

export default function PerformanceCharts({ frames, phases }) {
  const [mounted, setMounted] = useState(false);

  useEffect(() => {
    setMounted(true);
  }, []);

  if (!frames || frames.length === 0) return null;
  if (!mounted) return null;

  const frameData = frames.map(f => f.duration_ms.toFixed(2));
  const dirtyData = frames.map(f => (f.dirty_area_ratio * 100).toFixed(1));
  const categories = frames.map(f => f.frame_number);

  const frameOptions = {
    chart: {
      id: 'frame-durations',
      toolbar: { show: true },
      background: 'transparent',
      animations: { enabled: false }
    },
    theme: { mode: 'dark' },
    stroke: { curve: 'smooth', width: 2 },
    colors: ['#58a6ff'],
    fill: {
      type: 'gradient',
      gradient: {
        shadeIntensity: 1,
        opacityFrom: 0.45,
        opacityTo: 0.05,
        stops: [20, 100]
      }
    },
    xaxis: {
      categories: categories,
      title: { text: 'Frame Number', style: { color: '#8b949e' } },
      labels: { show: categories.length < 50, style: { colors: '#8b949e' } }
    },
    yaxis: {
      title: { text: 'Duration (ms)', style: { color: '#8b949e' } },
      labels: { style: { colors: '#8b949e' } }
    },
    tooltip: { theme: 'dark' },
    grid: { borderColor: '#30363d' },
    annotations: {
      yaxis: [
        {
          y: 16.67,
          borderColor: '#3fb950',
          label: {
            text: '60 FPS',
            style: { color: '#fff', background: '#3fb950' }
          }
        },
        {
          y: 33.33,
          borderColor: '#d29922',
          label: {
            text: '30 FPS',
            style: { color: '#fff', background: '#d29922' }
          }
        }
      ]
    }
  };

  const dirtyOptions = {
    ...frameOptions,
    id: 'dirty-ratio',
    colors: ['#d2a8ff'],
    yaxis: {
      title: { text: 'Dirty Area (%)', style: { color: '#8b949e' } },
      labels: { style: { colors: '#8b949e' } },
      max: 100
    },
    annotations: {}
  };

  // Phase Durations Pie Chart
  const filteredPhases = phases ? phases.filter(p => !p.phase_name.includes(':')) : [];
  const phaseLabels = filteredPhases.map(p => p.phase_name);
  const phaseValues = filteredPhases.map(p => p.duration_ms);

  const phaseOptions = {
    chart: { id: 'phase-breakdown', background: 'transparent' },
    theme: { mode: 'dark' },
    labels: phaseLabels,
    legend: { position: 'bottom', labels: { colors: '#8b949e' } },
    plotOptions: {
      pie: {
        donut: {
          size: '65%',
          labels: {
            show: true,
            name: { color: '#f0f6fc' },
            value: { color: '#8b949e' },
            total: {
              show: true,
              label: 'Total',
              color: '#f0f6fc',
              formatter: (w) => {
                return w.globals.seriesTotals.reduce((a, b) => a + b, 0).toFixed(1) + 'ms';
              }
            }
          }
        }
      }
    },
    stroke: { show: false },
    tooltip: { theme: 'dark' }
  };

  return (
    <div className="charts-container" style={{ marginBottom: '24px' }}>
      <div className="charts-grid" style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '16px' }}>
        <div className="glass-panel" style={{ padding: '16px' }}>
          <h3 style={{ marginBottom: '12px', fontSize: '0.9rem', color: '#f0f6fc' }}>Frame Duration Trend</h3>
          <Chart
            options={frameOptions}
            series={[{ name: 'Duration', data: frameData }]}
            type="area"
            height={250}
          />
        </div>
        <div className="glass-panel" style={{ padding: '16px' }}>
          <h3 style={{ marginBottom: '12px', fontSize: '0.9rem', color: '#f0f6fc' }}>Dirty Area Ratio (%)</h3>
          <Chart
            options={dirtyOptions}
            series={[{ name: 'Dirty Ratio', data: dirtyData }]}
            type="area"
            height={250}
          />
        </div>
      </div>
      
      {phaseValues.length > 0 && (
        <div className="glass-panel" style={{ padding: '16px', marginTop: '16px' }}>
          <h3 style={{ marginBottom: '12px', fontSize: '0.9rem', color: '#f0f6fc' }}>Core Phases Breakdown (Total Run)</h3>
          <div style={{ display: 'flex', justifyContent: 'center' }}>
            <Chart
              options={phaseOptions}
              series={phaseValues}
              type="donut"
              width={450}
            />
          </div>
        </div>
      )}
    </div>
  );
}
