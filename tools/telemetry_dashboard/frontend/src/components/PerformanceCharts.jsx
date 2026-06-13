import React, { useEffect, useState } from 'react';
import Chart from 'react-apexcharts';

export default function PerformanceCharts({ frames, phases, counters }) {
  // ── Program Cache hit rate from per-run counters ────────────────────
  const pcHits = Number((counters || []).find(c => c.counter_name === 'program_cache_hits')?.counter_value || 0);
  const pcMisses = Number((counters || []).find(c => c.counter_name === 'program_cache_misses')?.counter_value || 0);
  const pcTotal = pcHits + pcMisses;
  const pcHitRate = pcTotal > 0 ? (pcHits / pcTotal * 100) : 0;
  const pcEvictions = Number((counters || []).find(c => c.counter_name === 'program_cache_evictions')?.counter_value || 0);
  const [mounted, setMounted] = useState(false);

  useEffect(() => {
    setMounted(true);
  }, []);

  if (!frames || frames.length === 0) return null;
  if (!mounted) return null;

  const frameData = frames.map(f => Number(f.duration_ms || 0));
  const dirtyData = frames.map(f => Number(f.dirty_area_ratio || 0) * 100);
  const cacheData = frames.map(f => (f.cache_hit ? 100 : 0));
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

  const cacheOptions = {
    ...frameOptions,
    id: 'frame-cache-hit',
    colors: ['#3fb950'],
    yaxis: {
      title: { text: 'Cache Hit (%)', style: { color: '#8b949e' } },
      labels: { style: { colors: '#8b949e' } },
      max: 100,
      min: 0
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
              label: 'Accumulated Work',
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

  // ── Program Cache capacity trend data ──────────────────────────────
  // Only show the trend chart if capacity actually changed (auto-tuning active).
  const capacityData = frames.map(f => Number(f.program_cache_capacity || 0));
  const capacityVaried = capacityData.some(v => v !== capacityData[0]);
  const capacityMin = Math.min(...capacityData);
  const capacityMax = Math.max(...capacityData);
  const capacityRange = capacityMax - capacityMin;

  const capacityOptions = {
    chart: {
      id: 'program-cache-capacity-trend',
      toolbar: { show: true },
      background: 'transparent',
      animations: { enabled: false }
    },
    theme: { mode: 'dark' },
    stroke: { curve: 'stepline', width: 2 },
    colors: ['#f0883e'],
    fill: {
      type: 'gradient',
      gradient: {
        shadeIntensity: 1,
        opacityFrom: 0.35,
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
      title: { text: 'Cache Capacity (entries)', style: { color: '#8b949e' } },
      labels: { style: { colors: '#8b949e' } },
      min: capacityRange > 0 ? Math.max(0, capacityMin - Math.ceil(capacityRange * 0.2)) : 0,
      forceNiceScale: true
    },
    tooltip: {
      theme: 'dark',
      y: {
        formatter: (val) => `${val} entries`
      }
    },
    grid: { borderColor: '#30363d' },
    annotations: {}
  };

  // ── Program Cache radial gauge options ─────────────────────────────
  const pcHitRateColor = pcHitRate > 80 ? '#3fb950' : pcHitRate > 50 ? '#d29922' : '#f85149';
  const pcGaugeOptions = {
    chart: {
      id: 'program-cache-hit-rate',
      background: 'transparent',
      animations: { enabled: false }
    },
    theme: { mode: 'dark' },
    plotOptions: {
      radialBar: {
        startAngle: -135,
        endAngle: 135,
        hollow: {
          size: '60%',
          background: 'transparent'
        },
        track: {
          background: '#30363d',
          strokeWidth: '80%'
        },
        dataLabels: {
          name: {
            show: true,
            fontSize: '14px',
            color: '#8b949e',
            offsetY: -8
          },
          value: {
            show: true,
            fontSize: '28px',
            fontWeight: 700,
            color: pcHitRateColor,
            offsetY: 4,
            formatter: (val) => `${val.toFixed(1)}%`
          }
        }
      }
    },
    fill: {
      colors: [pcHitRateColor]
    },
    stroke: {
      lineCap: 'round'
    },
    labels: ['Program Cache Hit Rate'],
    tooltip: {
      theme: 'dark',
      y: {
        formatter: (val) => `${pcHits} hits / ${pcTotal} total`
      }
    }
  };

  const pcGaugeSeries = [pcHitRate];

  return (
    <div className="charts-container" style={{ marginBottom: '24px' }}>
      <div className="charts-grid" style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(280px, 1fr))', gap: '16px' }}>
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
            series={[{ name: 'Dirty Coverage', data: dirtyData }]}
            type="area"
            height={250}
          />
        </div>
        <div className="glass-panel" style={{ padding: '16px' }}>
          <h3 style={{ marginBottom: '12px', fontSize: '0.9rem', color: '#f0f6fc' }}>Frame Cache Hit (%)</h3>
          <Chart
            options={cacheOptions}
            series={[{ name: 'Cache Hit', data: cacheData }]}
            type="area"
            height={250}
          />
        </div>
        {capacityVaried && (
          <div className="glass-panel" style={{ padding: '16px' }}>
            <h3 style={{ marginBottom: '12px', fontSize: '0.9rem', color: '#f0f6fc' }}>
              Program Cache Capacity Trend
              <span style={{ marginLeft: '8px', fontSize: '0.7rem', color: '#8b949e' }}>
                (auto-tune active)
              </span>
            </h3>
            <Chart
              options={capacityOptions}
              series={[{ name: 'Capacity', data: capacityData }]}
              type="area"
              height={250}
            />
            <div style={{ display: 'flex', justifyContent: 'space-around', marginTop: '4px', fontSize: '0.75rem', color: '#8b949e' }}>
              <span>start: {capacityData[0]}</span>
              <span>end: {capacityData[capacityData.length - 1]}</span>
              <span>min: {capacityMin}</span>
              <span>max: {capacityMax}</span>
            </div>
          </div>
        )}
        {pcTotal > 0 && (
          <div className="glass-panel" style={{ padding: '16px' }}>
            <h3 style={{ marginBottom: '12px', fontSize: '0.9rem', color: '#f0f6fc' }}>
              Program Cache Hit Rate
            </h3>
            <Chart
              options={pcGaugeOptions}
              series={pcGaugeSeries}
              type="radialBar"
              height={250}
            />
            <div style={{ display: 'flex', justifyContent: 'space-around', marginTop: '4px', fontSize: '0.75rem', color: '#8b949e' }}>
              <span>{pcHits.toLocaleString()} hits</span>
              <span>{pcMisses.toLocaleString()} misses</span>
              <span>{pcEvictions.toLocaleString()} evictions</span>
            </div>
          </div>
        )}
      </div>
      
      {phaseValues.length > 0 && (
        <div className="glass-panel" style={{ padding: '16px', marginTop: '16px' }}>
          <h3 style={{ marginBottom: '12px', fontSize: '0.9rem', color: '#f0f6fc' }}>Core Phases Breakdown (Accumulated Work)</h3>
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
