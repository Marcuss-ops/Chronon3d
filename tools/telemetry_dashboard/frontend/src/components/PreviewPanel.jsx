import React, { useState, useEffect } from 'react';
import { outputPathToArtifactUrl } from '../api/telemetryApi.js';

export default function PreviewPanel({ run }) {
  if (!run) return null;

  const outputPath = run.output_path || '';
  const isVideoRun = outputPath.endsWith('.mp4') || outputPath.endsWith('.webm') || outputPath.endsWith('.mov');

  const [mode, setMode] = useState(isVideoRun ? 'video' : 'frame');

  // Automatically switch mode when run changes
  useEffect(() => {
    setMode(isVideoRun ? 'video' : 'frame');
  }, [run.run_id, isVideoRun]);

  // Resolve image frame path (replace #### template with 0000 frame)
  const resolvedFramePath = outputPath.includes('####')
    ? outputPath.replace('####', '0000')
    : outputPath;

  // Resolve video path (predict composition_id.mp4 location if it is a PNG template)
  let resolvedVideoPath = outputPath;
  if (outputPath.includes('####') || outputPath.endsWith('.png')) {
    resolvedVideoPath = `output/${run.composition_id}.mp4`;
  }

  const frameUrl = outputPathToArtifactUrl(resolvedFramePath);
  const videoUrl = outputPathToArtifactUrl(resolvedVideoPath);

  return (
    <section className="glass-panel preview-panel">
      <div className="panel-title" style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', flexWrap: 'wrap', gap: '12px' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: '16px', flexWrap: 'wrap' }}>
          <span style={{ fontWeight: 700, letterSpacing: '-0.02em' }}>Render Preview</span>
          <div className="preview-mode-toggles">
            <button 
              className={`preview-toggle-btn ${mode === 'video' ? 'active' : ''}`}
              onClick={() => setMode('video')}
            >
              🎥 Video Playback
            </button>
            <button 
              className={`preview-toggle-btn ${mode === 'frame' ? 'active' : ''}`}
              onClick={() => setMode('frame')}
            >
              🖼️ Frame #0000
            </button>
          </div>
        </div>
        <span className="preview-path" style={{ fontSize: '0.8rem', opacity: 0.6 }}>
          {mode === 'video' ? resolvedVideoPath : resolvedFramePath}
        </span>
      </div>

      <div className="preview-media-container" style={{ marginTop: '14px' }}>
        {mode === 'video' ? (
          <div style={{ display: 'flex', flexDirection: 'column', gap: '8px' }}>
            <video 
              className="render-preview-media animate-fade-in" 
              controls 
              playsInline 
              autoPlay
              loop
              muted
              src={videoUrl}
              style={{ marginTop: 0 }}
            />
            {!isVideoRun && (
              <span style={{ fontSize: '0.8rem', color: 'var(--text-secondary)', fontStyle: 'italic', display: 'block', textAlign: 'center', marginTop: '4px' }}>
                ℹ️ Questo run ha generato singoli frame (PNG). Stai visualizzando una predizione del video della composizione ({resolvedVideoPath}).
              </span>
            )}
          </div>
        ) : (
          <img 
            className="render-preview-media animate-fade-in" 
            src={frameUrl} 
            alt={run.composition_id}
            style={{ marginTop: 0 }}
          />
        )}
      </div>
    </section>
  );
}
