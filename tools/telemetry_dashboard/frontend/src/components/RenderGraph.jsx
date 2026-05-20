import React, { useState, useEffect } from 'react';
import ReactFlow, { 
  Background, 
  Controls, 
  MiniMap,
  useNodesState,
  useEdgesState
} from 'reactflow';
import 'reactflow/dist/style.css';

export default function RenderGraph({ compositionId }) {
  const [nodes, setNodes, onNodesChange] = useNodesState([]);
  const [edges, setEdges, onEdgesChange] = useEdgesState([]);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState('');

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

        // Layout nodes in a simple grid for now
        const flowNodes = data.nodes.map((n, i) => ({
          id: n.id,
          data: { label: n.label },
          position: { x: (i % 3) * 200, y: Math.floor(i / 3) * 100 },
          style: { 
            background: n.color, 
            color: '#000',
            fontWeight: 'bold',
            borderRadius: '8px',
            border: '1px solid #30363d'
          }
        }));

        const flowEdges = data.edges.map((e, i) => ({
          id: `e${i}`,
          source: e.source,
          target: e.target,
          animated: true,
          style: { stroke: '#58a6ff' }
        }));

        setNodes(flowNodes);
        setEdges(flowEdges);
        setError('');
      } catch (err) {
        setError('Failed to fetch graph data');
      } finally {
        setLoading(false);
      }
    };

    fetchGraph();
  }, [compositionId, setNodes, setEdges]);

  return (
    <div className="glass-panel" style={{ height: '500px', marginTop: '16px', position: 'relative' }}>
      <div className="panel-title" style={{ padding: '12px 16px', borderBottom: '1px solid #30363d' }}>
        <span>🕸️ Logical Render Graph Visualization</span>
        {loading && <span style={{ marginLeft: '12px', fontSize: '0.8rem', opacity: 0.6 }}>Generating...</span>}
      </div>
      
      {error ? (
        <div style={{ padding: '20px', color: 'var(--color-danger)' }}>{error}</div>
      ) : (
        <div style={{ width: '100%', height: 'calc(100% - 45px)' }}>
          <ReactFlow
            nodes={nodes}
            edges={edges}
            onNodesChange={onNodesChange}
            onEdgesChange={onEdgesChange}
            fitView
          >
            <Background color="#30363d" gap={20} />
            <Controls />
            <MiniMap 
              nodeColor={(n) => n.style?.background || '#fff'}
              maskColor="rgba(0, 0, 0, 0.1)"
            />
          </ReactFlow>
        </div>
      )}
    </div>
  );
}
