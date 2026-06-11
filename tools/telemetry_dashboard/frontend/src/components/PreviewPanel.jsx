import React, { useState, useEffect, useRef, useCallback } from 'react';
import { outputPathToArtifactUrl } from '../api/telemetryApi.js';

export default function PreviewPanel({ run, selectedFrame, nodeEvents }) {
  if (!run) return null;

  const outputPath = run.output_path || '';

  // Helper to check if a path has a video extension
  const isVideoExt = (p) => p.endsWith('.mp4') || p.endsWith('.webm') || p.endsWith('.mov');
  // Only treat as video if the actual output path is a video
  const isVideoRun = isVideoExt(outputPath);
  // Resolve video path only when it is actually a video
  const resolvedVideoPath = isVideoRun ? outputPath : `output/${run.composition_id}.mp4`;

  const [mode, setMode] = useState(isVideoRun ? 'video' : 'frame');
  const [mediaError, setMediaError] = useState('');
  const userModeOverrideRef = useRef(false);
  const videoRef = useRef(null);

  const tryPlayVideo = useCallback(() => {
    const el = videoRef.current;
    if (el && mode === 'video') {
      const p = el.play();
      if (p && typeof p.catch === 'function') {
        p.catch(() => {});
      }
    }
  }, [mode]);

  useEffect(() => {
    if (run.run_id) {
      userModeOverrideRef.current = false;
    }
  }, [run.run_id]);

  useEffect(() => {
    if (!userModeOverrideRef.current) {
      setMode(isVideoRun ? 'video' : 'frame');
    }
  }, [run.run_id, isVideoRun, selectedFrame]);

  useEffect(() => {
    if (mode === 'video') {
      tryPlayVideo();
    }
  }, [mode, tryPlayVideo]);

  const resolvedFramePath = (() => {
    let path = outputPath.includes('####')
      ? outputPath.replace('####', (selectedFrame?.frame_number || 0).toString().padStart(4, '0'))
      : outputPath;

    // When output is a video, derive frame PNG path from basename (e.g. output/focus_quote.mp4 → output/focus_quote_frame.png)
    if (path.endsWith('.mp4') || path.endsWith('.webm') || path.endsWith('.mov')) {
      path = path.replace(/\.(mp4|webm|mov)$/, '_frame.png');
    }

    // If frame path is empty, also fall back to the composition frame PNG
    if (!path) {
      path = `output/${run.composition_id}_frame.png`;
    }

    return path;
  })();

  const previewVersion = `${run.run_id}:${selectedFrame?.frame_number ?? 'base'}:${run.finished_at_iso || ''}`;
  const frameUrl = outputPathToArtifactUrl(resolvedFramePath, previewVersion);
  const videoUrl = outputPathToArtifactUrl(resolvedVideoPath, previewVersion);
  const downloadName = `render_${(selectedFrame?.frame_number || 0).toString().padStart(4, '0')}.png`;

  const downloadFrame = async () => {
    setMediaError('');
    try {
      const response = await fetch(frameUrl);
      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
      }

      const blob = await response.blob();
      const blobUrl = URL.createObjectURL(blob);
      const link = document.createElement('a');
      link.href = blobUrl;
      link.download = downloadName;
      link.style.display = 'none';
      document.body.appendChild(link);
      link.click();
      link.remove();
      URL.revokeObjectURL(blobUrl);
    } catch (error) {
      setMediaError(`Download unavailable: ${error.message}`);
    }
  };

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
                onClick={() => {
                  userModeOverrideRef.current = true;
                  setMode('video');
                }}
              >
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor">
                  <polygon points="5 3 19 12 5 21 5 3"></polygon>
                </svg>
                Playback
              </button>
              <button 
                className={`preview-toggle-item ${mode === 'frame' ? 'active' : ''}`}
                onClick={() => {
                  userModeOverrideRef.current = true;
                  setMode('frame');
                }}
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
                onClick={() => {
                  userModeOverrideRef.current = true;
                  setMode('heatmap');
                }}
                title="Visualizza aree ridisegnate (Dirty Rects)"
              >
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor">
                  <path d="M12 2v20M2 12h20" strokeWidth="2" stroke="currentColor"/>
                  <path d="M12 2a10 10 0 1 0 10 10A10 10 0 0 0 12 2zm0 18a8 8 0 1 1 8-8 8 8 0 0 1-8 8z" strokeWidth="1" stroke="currentColor"/>
                </svg>
                Heatmap
              </button>
              <button
                type="button"
                onClick={downloadFrame}
                className="preview-toggle-item"
                style={{ display: 'inline-flex', alignItems: 'center', color: 'var(--text-primary)' }}
                title="Scarica PNG del frame selezionato"
              >
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" style={{ width: '14px', height: '14px', marginRight: '4px' }}>
                  <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4" strokeWidth="2" strokeLinecap="round"/>
                  <polyline points="7 10 12 15 17 10" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"/>
                  <line x1="12" y1="15" x2="12" y2="3" strokeWidth="2" strokeLinecap="round"/>
                </svg>
                Download PNG
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
              preload="auto"
              key={videoUrl}
              src={videoUrl}
              ref={videoRef}
              onCanPlay={tryPlayVideo}
              onLoadedData={() => {
                setMediaError('');
                tryPlayVideo();
              }}
              onError={() => setMediaError('Video preview unavailable')}
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
