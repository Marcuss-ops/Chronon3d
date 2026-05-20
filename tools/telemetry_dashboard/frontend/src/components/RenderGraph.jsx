import React, { useState, useEffect } from 'react';
import ReactFlow, { 
  Background, 
  Controls, 
  MiniMap,
  useNodesState,
  useEdgesState
} from 'reactflow';
import 'reactflow/dist/style.css';
import { getAggregatedNodes } from '../utils/aggregate.js';
import { formatBytes } from '../utils/format.jsx';

export default function RenderGraph({ compositionId, runDetail }) {
  const [nodes, setNodes, onNodesChange] = useNodesState([]);
  const [edges, setEdges, onEdgesChange] = useEdgesState([]);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState('');

  const [graphData, setGraphData] = useState(null);
  const [layoutDir, setLayoutDir] = useState('TB');
  const [searchQuery, setSearchQuery] = useState('');
  const [selectedNodeId, setSelectedNodeId] = useState(null);

  // Fetch Graph Data
  useEffect(() => {
    if (!compositionId) return;

    const fetchGraph = async () => {
      setLoading(true);
      try {
        const resp = await fetch(`http://localhost:8000/api/graph/${compositionId}`);
        const data = await resp.json();
        
        if (data.error) {
          setError(data.error);
          return;
        }

        setGraphData(data);
        setError('');
      } catch (err) {
        setError('Failed to fetch graph data');
      } finally {
        setLoading(false);
      }
    };

    fetchGraph();
  }, [compositionId]);

  // Compute Layout (Topological Generation Layers)
  useEffect(() => {
    if (!graphData) return;

    const aggregatedNodes = runDetail ? getAggregatedNodes(runDetail) : [];

    // 1. Build adjacency list of who depends on whom
    const adj = {};
    const inDegree = {};
    graphData.nodes.forEach(n => {
      adj[n.id] = [];
      inDegree[n.id] = 0;
    });

    graphData.edges.forEach(e => {
      if (adj[e.source]) {
        adj[e.source].push(e.target);
      }
      if (inDegree[e.target] !== undefined) {
        inDegree[e.target]++;
      }
    });

    // 2. Compute levels using BFS
    const levels = {};
    let queue = graphData.nodes.filter(n => inDegree[n.id] === 0).map(n => n.id);
    
    if (queue.length === 0 && graphData.nodes.length > 0) {
      queue = [graphData.nodes[0].id];
    }

    queue.forEach(id => {
      levels[id] = 0;
    });

    let qIdx = 0;
    while (qIdx < queue.length) {
      const curr = queue[qIdx++];
      const currLvl = levels[curr] || 0;
      
      if (adj[curr]) {
        adj[curr].forEach(next => {
          const nextLvl = levels[next] || 0;
          if (currLvl + 1 > nextLvl) {
            levels[next] = currLvl + 1;
            if (!queue.includes(next)) {
              queue.push(next);
            }
          }
        });
      }
    }

    // Group nodes by level
    const levelGroups = {};
    graphData.nodes.forEach(n => {
      const lvl = levels[n.id] || 0;
      if (!levelGroups[lvl]) {
        levelGroups[lvl] = [];
      }
      levelGroups[lvl].push(n);
    });

    const flowNodes = [];
    const levelsSorted = Object.keys(levelGroups).map(Number).sort((a, b) => a - b);
    
    levelsSorted.forEach(lvl => {
      const levelNodes = levelGroups[lvl];
      const lvlCount = levelNodes.length;
      
      levelNodes.forEach((n, idx) => {
        const isMatch = searchQuery && n.label.toLowerCase().includes(searchQuery.toLowerCase());
        const isSelected = selectedNodeId === n.id;
        
        let x = 0;
        let y = 0;

        if (layoutDir === 'TB') {
          const hGap = 260;
          const vGap = 200; // Increased spacing for a taller vertical layout!
          const xOffset = ((lvlCount - 1) * hGap) / 2;
          x = idx * hGap - xOffset + 400;
          y = lvl * vGap + 80;
        } else {
          const hGap = 260;
          const vGap = 140;
          const yOffset = ((lvlCount - 1) * vGap) / 2;
          x = lvl * hGap + 100;
          y = idx * vGap - yOffset + 300;
        }

        // Fetch duration to render directly in the node label
        const stats = aggregatedNodes.find(an => an.node_name.toLowerCase() === n.label.toLowerCase());

        const labelContent = (
          <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center' }}>
            <div style={{ fontWeight: 'bold' }}>{n.label}</div>
            {stats && (
              <div style={{ 
                fontSize: '0.72rem', 
                color: '#3b3e4f', 
                opacity: 0.95, 
                marginTop: '4px', 
                borderTop: '1px solid rgba(0,0,0,0.12)', 
                paddingTop: '4px', 
                fontWeight: 600,
                width: '100%',
                textAlign: 'center'
              }}>
                ⏱️ {stats.duration_ms.toFixed(2)} ms
              </div>
            )}
          </div>
        );

        flowNodes.push({
          id: n.id,
          data: { label: labelContent },
          position: { x, y },
          style: { 
            background: n.color, 
            color: '#1f2428',
            borderRadius: '8px',
            border: isSelected 
              ? '3px solid #8b5cf6' 
              : (isMatch ? '2px solid #58a6ff' : '1px solid #30363d'),
            boxShadow: isSelected 
              ? '0 0 20px rgba(139, 92, 246, 0.8), 0 4px 6px rgba(0,0,0,0.1)'
              : (isMatch 
                  ? '0 0 15px rgba(88, 166, 255, 0.6), 0 4px 6px rgba(0,0,0,0.1)' 
                  : '0 4px 6px -1px rgba(0,0,0,0.1), 0 2px 4px -1px rgba(0,0,0,0.06)'
                ),
            padding: '10px 14px',
            fontSize: '0.85rem',
            minWidth: '130px',
            textAlign: 'center',
            cursor: 'pointer',
            transition: 'border-color 0.2s, box-shadow 0.2s'
          }
        });
      });
    });

    const flowEdges = graphData.edges.map((e, i) => {
      const sourceNode = graphData.nodes.find(n => n.id === e.source);
      const targetNode = graphData.nodes.find(n => n.id === e.target);
      
      const sourceMatch = searchQuery && sourceNode?.label.toLowerCase().includes(searchQuery.toLowerCase());
      const targetMatch = searchQuery && targetNode?.label.toLowerCase().includes(searchQuery.toLowerCase());
      const isSearchActive = !!searchQuery;
      
      return {
        id: `e${i}`,
        source: e.source,
        target: e.target,
        animated: true,
        style: { 
          stroke: isSearchActive && (sourceMatch || targetMatch) ? '#58a6ff' : 'rgba(88, 166, 255, 0.3)',
          strokeWidth: isSearchActive && (sourceMatch || targetMatch) ? 3 : 1.5,
          transition: 'stroke 0.2s, stroke-width 0.2s'
        }
      };
    });

    setNodes(flowNodes);
    setEdges(flowEdges);
  }, [graphData, layoutDir, searchQuery, selectedNodeId, runDetail, setNodes, setEdges]);

  // Lookup node details
  const selectedNodeData = graphData && selectedNodeId 
    ? graphData.nodes.find(n => n.id === selectedNodeId)
    : null;

  const aggregatedNodes = runDetail ? getAggregatedNodes(runDetail) : [];
  const selectedNodeStats = selectedNodeData 
    ? aggregatedNodes.find(an => an.node_name.toLowerCase() === selectedNodeData.label.toLowerCase())
    : null;

  return (
    <div className="glass-panel animate-fade-in" style={{ height: '950px', marginTop: '16px', position: 'relative', display: 'flex', flexDirection: 'column', overflow: 'hidden' }}>
      
      {/* Header Row */}
      <div className="panel-title" style={{ padding: '12px 16px', borderBottom: '1px solid #30363d', display: 'flex', justifyContent: 'space-between', alignItems: 'center', flexShrink: 0 }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
          <span>🕸️ Interactive Logical Render Graph</span>
          {loading && <span style={{ fontSize: '0.8rem', opacity: 0.6 }}>Generating...</span>}
        </div>
        
        {/* Layout Orientation Controls */}
        {!error && graphData && (
          <div style={{ display: 'flex', gap: '8px', alignItems: 'center' }}>
            <span style={{ fontSize: '0.75rem', color: 'var(--text-secondary)' }}>Layout Flow:</span>
            <button 
              className={`preview-toggle-item ${layoutDir === 'TB' ? 'active' : ''}`}
              onClick={() => setLayoutDir('TB')}
              style={{ 
                padding: '4px 10px', 
                fontSize: '0.75rem', 
                borderRadius: '6px', 
                cursor: 'pointer', 
                border: '1px solid var(--border-color)', 
                background: layoutDir === 'TB' ? 'rgba(139, 92, 246, 0.15)' : 'rgba(255,255,255,0.02)', 
                color: layoutDir === 'TB' ? '#a78bfa' : 'var(--text-secondary)',
                fontWeight: 600
              }}
            >
              ⬇️ Vertical
            </button>
            <button 
              className={`preview-toggle-item ${layoutDir === 'LR' ? 'active' : ''}`}
              onClick={() => setLayoutDir('LR')}
              style={{ 
                padding: '4px 10px', 
                fontSize: '0.75rem', 
                borderRadius: '6px', 
                cursor: 'pointer', 
                border: '1px solid var(--border-color)', 
                background: layoutDir === 'LR' ? 'rgba(139, 92, 246, 0.15)' : 'rgba(255,255,255,0.02)', 
                color: layoutDir === 'LR' ? '#a78bfa' : 'var(--text-secondary)',
                fontWeight: 600
              }}
            >
              ➡️ Horizontal
            </button>
          </div>
        )}
      </div>

      {/* Interactive Search Overlay */}
      {!error && graphData && (
        <div style={{ padding: '10px 16px', borderBottom: '1px solid #30363d', display: 'flex', gap: '12px', alignItems: 'center', background: 'rgba(0,0,0,0.15)', flexShrink: 0 }}>
          <span style={{ fontSize: '0.8rem', color: 'var(--text-secondary)', fontWeight: 600 }}>🔍 Search Node:</span>
          <input 
            type="text" 
            placeholder="Search by node name (e.g. Clear, Background, Text)..." 
            value={searchQuery}
            onChange={(e) => setSearchQuery(e.target.value)}
            style={{ 
              flex: 1, 
              background: 'rgba(255,255,255,0.03)', 
              border: '1px solid #30363d', 
              borderRadius: '6px', 
              padding: '6px 12px', 
              color: '#fff', 
              fontSize: '0.85rem', 
              outline: 'none',
              transition: 'border-color 0.2s' 
            }}
          />
          {searchQuery && (
            <button 
              onClick={() => setSearchQuery('')}
              style={{ background: 'transparent', border: 'none', color: '#58a6ff', cursor: 'pointer', fontSize: '0.85rem', fontWeight: 600 }}
            >
              Clear
            </button>
          )}
        </div>
      )}

      {/* Main Split Area */}
      {error ? (
        <div style={{ padding: '20px', color: 'var(--color-danger)' }}>{error}</div>
      ) : (
        <div style={{ display: 'flex', flex: 1, overflow: 'hidden', position: 'relative' }}>
          
          {/* Left/Center: Canvas */}
          <div style={{ flex: 1, height: '100%', position: 'relative' }}>
            <ReactFlow
              nodes={nodes}
              edges={edges}
              onNodesChange={onNodesChange}
              onEdgesChange={onEdgesChange}
              onNodeClick={(e, node) => setSelectedNodeId(node.id)}
              onPaneClick={() => setSelectedNodeId(null)}
              fitView
            >
              <Background color="#30363d" gap={20} />
              <Controls />
              <MiniMap 
                nodeColor={(n) => n.style?.background || '#fff'}
                maskColor="rgba(0, 0, 0, 0.1)"
              />
            </ReactFlow>
            
            {/* Quick helper tip overlay */}
            <div style={{ position: 'absolute', bottom: '12px', left: '12px', background: 'rgba(0,0,0,0.6)', padding: '6px 12px', borderRadius: '4px', fontSize: '0.75rem', color: 'var(--text-muted)', border: '1px solid #30363d', pointerEvents: 'none' }}>
              💡 Click a node to view its detailed execution profile
            </div>
          </div>

          {/* Right: Drawer panel for selected node */}
          {selectedNodeStats && (
            <div style={{ width: '340px', borderLeft: '1px solid #30363d', background: 'rgba(21, 26, 35, 0.96)', display: 'flex', flexDirection: 'column', height: '100%', flexShrink: 0, animation: 'slideIn 0.2s ease-out', backdropFilter: 'blur(8px)' }}>
              
              {/* Drawer Header */}
              <div style={{ padding: '16px', borderBottom: '1px solid #30363d', display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                <div>
                  <h4 style={{ margin: 0, fontSize: '1rem', color: '#fff', fontWeight: 700 }}>{selectedNodeStats.node_name}</h4>
                  <span style={{ fontSize: '0.75rem', fontFamily: 'var(--font-mono)', color: 'var(--text-secondary)' }}>{selectedNodeStats.node_type}</span>
                </div>
                <button 
                  onClick={() => setSelectedNodeId(null)}
                  style={{ background: 'transparent', border: 'none', color: 'var(--text-secondary)', cursor: 'pointer', fontSize: '1.2rem', padding: '4px' }}
                >
                  ✕
                </button>
              </div>

              {/* Drawer Content */}
              <div style={{ padding: '16px', overflowY: 'auto', flex: 1, display: 'flex', flexDirection: 'column', gap: '16px' }}>
                
                {/* Duration */}
                <div style={{ padding: '12px', background: 'rgba(139, 92, 246, 0.08)', border: '1px solid rgba(139, 92, 246, 0.2)', borderRadius: '8px' }}>
                  <div style={{ fontSize: '0.75rem', color: '#c084fc', textTransform: 'uppercase', letterSpacing: '0.05em', fontWeight: 600 }}>Duration (Avg)</div>
                  <div style={{ fontSize: '1.5rem', fontWeight: 800, color: '#c084fc', marginTop: '4px' }}>
                    {selectedNodeStats.duration_ms.toFixed(2)} <span style={{ fontSize: '0.9rem', fontWeight: 400 }}>ms</span>
                  </div>
                </div>

                {/* Executions & Cache hit rate */}
                <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '10px' }}>
                  <div style={{ padding: '12px', background: 'rgba(255,255,255,0.02)', border: '1px solid #30363d', borderRadius: '8px' }}>
                    <div style={{ fontSize: '0.7rem', color: 'var(--text-secondary)', textTransform: 'uppercase', fontWeight: 600 }}>Executions</div>
                    <div style={{ fontSize: '1.1rem', fontWeight: 700, marginTop: '4px', color: '#fff' }}>{selectedNodeStats.executions}x</div>
                  </div>
                  <div style={{ padding: '12px', background: 'rgba(255,255,255,0.02)', border: '1px solid #30363d', borderRadius: '8px' }}>
                    <div style={{ fontSize: '0.7rem', color: 'var(--text-secondary)', textTransform: 'uppercase', fontWeight: 600 }}>Cache Status</div>
                    <div style={{ marginTop: '6px' }}>
                      <span className={`cache-badge cache-badge-${selectedNodeStats.cache_status}`} style={{ fontSize: '0.75rem', padding: '2px 8px', borderRadius: '4px' }}>
                        {selectedNodeStats.cache_status === 'hit' ? 'Hit' : (selectedNodeStats.cache_status === 'miss' ? 'Miss' : (selectedNodeStats.cache_status === 'bypass' ? 'Bypass' : `Hit: ${(selectedNodeStats.hit_rate * 100).toFixed(0)}%`))}
                      </span>
                    </div>
                  </div>
                </div>

                {/* Dimensions */}
                <div style={{ padding: '12px', background: 'rgba(255,255,255,0.02)', border: '1px solid #30363d', borderRadius: '8px' }}>
                  <div style={{ fontSize: '0.75rem', color: 'var(--text-secondary)', textTransform: 'uppercase', fontWeight: 600 }}>Output Resolution</div>
                  <div style={{ fontSize: '1rem', fontWeight: 700, marginTop: '4px', color: '#fff' }}>
                    {selectedNodeStats.output_width} × {selectedNodeStats.output_height}
                  </div>
                  <div style={{ fontSize: '0.75rem', color: 'var(--text-muted)', marginTop: '2px' }}>
                    Memory Size: {formatBytes(selectedNodeStats.output_bytes)}
                  </div>
                </div>

                {/* Pixel Operations */}
                <div>
                  <div style={{ fontSize: '0.75rem', color: 'var(--text-secondary)', textTransform: 'uppercase', letterSpacing: '0.05em', marginBottom: '8px', fontWeight: 600 }}>Pixel Workload (Avg)</div>
                  
                  <div style={{ display: 'flex', flexDirection: 'column', gap: '8px' }}>
                    {[
                      { label: 'Touched', val: selectedNodeStats.pixels_touched },
                      { label: 'Cleared', val: selectedNodeStats.pixels_cleared },
                      { label: 'Composited', val: selectedNodeStats.pixels_composited },
                      { label: 'Transformed', val: selectedNodeStats.pixels_transformed },
                      { label: 'Blurred', val: selectedNodeStats.pixels_blurred }
                    ].map(op => (
                      <div key={op.label} style={{ display: 'flex', justifyContent: 'space-between', fontSize: '0.8rem', padding: '6px 0', borderBottom: '1px solid rgba(255,255,255,0.03)' }}>
                        <span style={{ color: 'var(--text-secondary)' }}>{op.label}:</span>
                        <span style={{ fontWeight: 600, fontFamily: 'var(--font-mono)', color: '#fff' }}>{op.val.toLocaleString()} px</span>
                      </div>
                    ))}
                  </div>
                </div>

              </div>

            </div>
          )}

          {/* Quick empty state for selected structural nodes */}
          {selectedNodeId && !selectedNodeStats && selectedNodeData && (
            <div style={{ width: '340px', borderLeft: '1px solid #30363d', background: 'rgba(21, 26, 35, 0.96)', padding: '16px', display: 'flex', flexDirection: 'column', height: '100%', flexShrink: 0, backdropFilter: 'blur(8px)' }}>
              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', borderBottom: '1px solid #30363d', paddingBottom: '12px' }}>
                <div>
                  <h4 style={{ margin: 0, color: '#fff', fontWeight: 700 }}>{selectedNodeData.label}</h4>
                  <span style={{ fontSize: '0.75rem', color: 'var(--text-secondary)' }}>Structural Node</span>
                </div>
                <button onClick={() => setSelectedNodeId(null)} style={{ background: 'transparent', border: 'none', color: 'var(--text-secondary)', cursor: 'pointer', fontSize: '1.2rem' }}>✕</button>
              </div>
              <p style={{ fontSize: '0.85rem', color: 'var(--text-muted)', marginTop: '20px', lineHeight: '1.4' }}>
                No runtime telemetry performance events are recorded for this structural helper node.
              </p>
            </div>
          )}

        </div>
      )}
    </div>
  );
}
