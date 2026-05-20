import React, { useState, useEffect } from 'react';
import { outputPathToArtifactUrl } from '../api/telemetryApi.js';

export default function PreviewPanel({ run, selectedFrame, nodeEvents }) {
  if (!run) return null;

  const outputPath = run.output_path || '';
  const isVideoRun = outputPath.endsWith('.mp4') || outputPath.endsWith('.webm') || outputPath.endsWith('.mov');

  const [mode, setMode] = useState(isVideoRun ? 'video' : 'frame');
  const [mediaError, setMediaError] = useState('');

  useEffect(() => {
    if (!selectedFrame) {
      setMode(isVideoRun ? 'video' : 'frame');
    }
  }, [run.run_id, isVideoRun, selectedFrame]);

  const resolvedFramePath = outputPath.includes('####')
    ? outputPath.replace('####', (selectedFrame?.frame_number || 0).toString().padStart(4, '0'))
    : outputPath;

  let resolvedVideoPath = outputPath;
  if (outputPath.includes('####') || outputPath.endsWith('.png')) {
    resolvedVideoPath = `output/${run.composition_id}.mp4`;
  }

  const previewVersion = `${run.run_id}:${selectedFrame?.frame_number ?? 'base'}:${run.finished_at_iso || ''}`;
  const frameUrl = outputPathToArtifactUrl(resolvedFramePath, previewVersion);
  const videoUrl = outputPathToArtifactUrl(resolvedVideoPath, previewVersion);

  // Filter node events for spatial heatmap
  const currentFrameNodes = selectedFrame && nodeEvents
    ? nodeEvents.filter(n => n.frame_number === selectedFrame.frame_number && n.cache_status !== 'hit' && n.bbox_w > 0)
    : [];

  return (
    <section className="glass-panel preview-panel">
      <div className="preview-container">
        <div className="preview-header">
          <div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
            <span style={{ fontSize: '0.9rem', fontWeight: 800, color: 'var(--text-primary)', textTransform: 'uppercase', letterSpacing: '0.05em' }}>
              Output Preview
            </span>
            <div className="preview-toggle-group">
              <button 
                className={`preview-toggle-item ${mode === 'video' ? 'active' : ''}`}
                onClick={() => setMode('video')}
              >
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor">
                  <polygon points="5 3 19 12 5 21 5 3"></polygon>
                </svg>
                Playback
              </button>
              <button 
                className={`preview-toggle-item ${mode === 'frame' ? 'active' : ''}`}
                onClick={() => setMode('frame')}
              >
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor">
                  <rect x="3" y="3" width="18" height="18" rx="2" ry="2"></rect>
                  <circle cx="8.5" cy="8.5" r="1.5"></circle>
                  <polyline points="21 15 16 10 5 21"></polyline>
                </svg>
                {selectedFrame ? `Frame ${selectedFrame.frame_number}` : 'Frame 0'}
              </button>
              <button 
                className={`preview-toggle-item ${mode === 'heatmap' ? 'active' : ''}`}
                onClick={() => setMode('heatmap')}
                title="Visualizza aree ridisegnate (Dirty Rects)"
              >
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor">
                  <path d="M12 2v20M2 12h20" strokeWidth="2" stroke="currentColor"/>
                  <path d="M12 2a10 10 0 1 0 10 10A10 10 0 0 0 12 2zm0 18a8 8 0 1 1 8-8 8 8 0 0 1-8 8z" strokeWidth="1" stroke="currentColor"/>
                </svg>
                Heatmap
              </button>
            </div>
          </div>
          <div className="preview-badge">
            {mode === 'video' ? 'H.264 / MP4' : mode === 'heatmap' ? 'DIRTY RECTS' : 'PNG / 16-BIT'}
          </div>
        </div>

        <div className="preview-media-wrapper animate-fade-in" style={{ position: 'relative' }}>
          {mode === 'video' ? (
            <video 
              className="preview-media-content" 
              controls 
              playsInline 
              autoPlay
              loop
              muted
              key={videoUrl}
              src={videoUrl}
              onError={() => setMediaError('Video preview unavailable')}
              onLoadedData={() => setMediaError('')}
            />
          ) : (
            <>
              <img 
                className="preview-media-content" 
                src={frameUrl} 
                alt="Preview"
                key={frameUrl}
                onError={() => setMediaError('Frame preview unavailable')}
                onLoad={() => setMediaError('')}
              />
              {mode === 'heatmap' && (
                <div className="heatmap-overlay">
                  {currentFrameNodes.map((node, idx) => {
                    const maxWidth = Math.max(...nodeEvents.map(n => n.bbox_x + n.bbox_w), 1920);
                    const maxHeight = Math.max(...nodeEvents.map(n => n.bbox_y + n.bbox_h), 1080);
                    
                    return (
                      <div 
                        key={idx}
                        className="dirty-rect"
                        style={{
                          left: `${(node.bbox_x / maxWidth) * 100}%`,
                          top: `${(node.bbox_y / maxHeight) * 100}%`,
                          width: `${(node.bbox_w / maxWidth) * 100}%`,
                          height: `${(node.bbox_h / maxHeight) * 100}%`,
                          background: `rgba(239, 68, 68, ${Math.min(0.2 + (node.duration_ms / 10), 0.7)})`,
                          border: '1px solid rgba(239, 68, 68, 0.8)',
                        }}
                        title={`${node.node_name} (${node.node_type}): ${node.duration_ms.toFixed(2)}ms`}
                      />
                    );
                  })}
                  {currentFrameNodes.length === 0 && (
                    <div className="heatmap-empty-msg">
                      {!selectedFrame ? "Seleziona un frame dalla timeline per vedere la heatmap" : "Nessuna area ridisegnata (Cache Hit totale)"}
                    </div>
                  )}
                </div>
              )}
              {mediaError && (
                <div className="heatmap-empty-msg">
                  {mediaError}
                </div>
              )}
            </>
          )}
          
          <div className="preview-overlay-info">
            <span className="preview-filename">
              {mode === 'video' ? resolvedVideoPath : resolvedFramePath}
            </span>
            {mode === 'heatmap' && selectedFrame && (
              <span style={{ fontSize: '0.65rem', color: 'var(--color-accent)', fontWeight: 700 }}>
                {currentFrameNodes.length} DIRTY REGIONS DETECTED
              </span>
            )}
          </div>
        </div>
      </div>
    </section>
  );
}
