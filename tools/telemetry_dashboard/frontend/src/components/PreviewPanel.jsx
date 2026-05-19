import React, { useState } from 'react';
import { outputPathToArtifactUrl } from '../api/telemetryApi.js';

export default function PreviewPanel({ run }) {
  if (!run) return null;

  const [mode, setMode] = useState('video'); // default to video playback

  const outputPath = run.output_path || '';
  
  // Resolve image frame path (replace #### template with 0000 frame)
  const resolvedFramePath = outputPath.includes('####')
    ? outputPath.replace('####', '0000')
    : outputPath;

  // Resolve video path (predict lil_dirk.mp4 location if it is a PNG template)
  let resolvedVideoPath = outputPath;
  if (outputPath.includes('####') || outputPath.endsWith('.png')) {
    resolvedVideoPath = 'output/lil_dirk.mp4';
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
