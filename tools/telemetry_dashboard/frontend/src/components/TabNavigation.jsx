import React from 'react';

export default function TabNavigation({ activeTab, onTabChange, layerCount, nodeCount }) {
  return (
    <div className="tab-navigation">
      <button
        className={`tab-btn ${activeTab === 'overview' ? 'active' : ''}`}
        onClick={() => onTabChange('overview')}
      >
        📊 Run Overview
      </button>
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
        className={`tab-btn ${activeTab === 'query_console' ? 'active' : ''}`}
        onClick={() => onTabChange('query_console')}
      >
        💻 SQL Analysis Console
      </button>
    </div>
  );
}
