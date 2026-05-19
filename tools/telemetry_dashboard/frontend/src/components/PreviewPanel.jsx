import React from 'react';
import { outputPathToArtifactUrl, isVideoOutput, isImageOutput } from '../api/telemetryApi.js';

export default function PreviewPanel({ run }) {
  if (!run) return null;

  const outputPath = run.output_path || '';
  const artifactUrl = outputPathToArtifactUrl(outputPath);
  const canPreview = Boolean(outputPath) && (isVideoOutput(outputPath) || isImageOutput(outputPath));

  return (
    <section className="glass-panel preview-panel">
      <div className="panel-title">
        <span>Render Preview</span>
        <span className="preview-path">{outputPath || 'No output path'}</span>
      </div>
      {canPreview ? (
        isVideoOutput(outputPath) ? (
          <video className="render-preview-media" controls playsInline src={artifactUrl} />
        ) : (
          <img className="render-preview-media" src={artifactUrl} alt={run.composition_id} />
        )
      ) : (
        <div className="preview-empty">
          No preview available for this output type.
        </div>
      )}
    </section>
  );
}
