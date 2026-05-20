import React from 'react';

export default function TabNavigation({ activeTab, onTabChange, layerCount, nodeCount, hasComparison }) {
  return (
    <div className="tab-navigation">
      <button
        className={`tab-btn ${activeTab === 'overview' ? 'active' : ''}`}
        onClick={() => onTabChange('overview')}
      >
        📊 Run Overview
      </button>
      {hasComparison && (
        <button
          className={`tab-btn ${activeTab === 'comparison' ? 'active' : ''}`}
          onClick={() => onTabChange('comparison')}
          style={{ borderColor: 'var(--color-accent)', color: 'var(--color-accent)' }}
        >
          ⚖️ A/B Comparison
        </button>
      )}
      <button
        className={`tab-btn ${activeTab === 'layers' ? 'active' : ''}`}
        onClick={() => onTabChange('layers')}
      >
        🥞 Layers Telemetry ({layerCount})
      </button>
      <button
        className={`tab-btn ${activeTab === 'nodes' ? 'active' : ''}`}
        onClick={() => onTabChange('nodes')}
      >
        🌳 Node Execution ({nodeCount})
      </button>
      <button
        className={`tab-btn ${activeTab === 'graph' ? 'active' : ''}`}
        onClick={() => onTabChange('graph')}
      >
        🕸️ Render Graph
      </button>
    </div>
  );
}
