import React, { useState } from 'react';
import { outputPathToArtifactUrl } from '../api/telemetryApi.js';

const ARTIFACTS = [
  {
    title: 'Buttery Smooth',
    subtitle: 'output/premium_thumbnail_buttery_smooth.png',
    path: 'output/premium_thumbnail_buttery_smooth.png',
  },
  {
    title: 'SaaS Blue',
    subtitle: 'output/premium_thumbnail_saas_blue.png',
    path: 'output/premium_thumbnail_saas_blue.png',
  },
  {
    title: 'Buttery Smooth Smoke',
    subtitle: 'output/visual_smoke/PremiumThumbnailButterySmooth.png',
    path: 'output/visual_smoke/PremiumThumbnailButterySmooth.png',
  },
  {
    title: 'SaaS Blue Smoke',
    subtitle: 'output/visual_smoke/PremiumThumbnailSaaSBlue.png',
    path: 'output/visual_smoke/PremiumThumbnailSaaSBlue.png',
  },
  {
    title: 'Camera Rig Center',
    subtitle: 'build/chronon/linux-release/test_renders/golden/camera_rig/center.png',
    path: 'build/chronon/linux-release/test_renders/golden/camera_rig/center.png',
  },
  {
    title: 'Camera Rig Orbit',
    subtitle: 'build/chronon/linux-release/test_renders/golden/camera_rig/orbit_120.png',
    path: 'build/chronon/linux-release/test_renders/golden/camera_rig/orbit_120.png',
  },
];

function ArtifactCard({ artifact, cacheToken }) {
  const [failed, setFailed] = useState(false);
  const url = outputPathToArtifactUrl(artifact.path, cacheToken);

  return (
    <div className="featured-artifact-card glass-panel">
      <div className="featured-artifact-image-wrap">
        {!failed ? (
          <img
            className="featured-artifact-image"
            src={url}
            alt={artifact.title}
            onError={() => setFailed(true)}
          />
        ) : (
          <div className="featured-artifact-fallback">
            Artifact unavailable
          </div>
        )}
      </div>
      <div className="featured-artifact-meta">
        <div className="featured-artifact-title">{artifact.title}</div>
        <div className="featured-artifact-path">{artifact.subtitle}</div>
      </div>
    </div>
  );
}

export default function FeaturedArtifacts() {
  const [cacheToken] = useState(() => String(Date.now()));

  return (
    <section className="featured-artifacts">
      <div className="featured-artifacts-header">
        <div>
          <div className="featured-artifacts-kicker">Latest artifacts</div>
          <h2 className="featured-artifacts-title">New renders and camera checks</h2>
        </div>
        <div className="featured-artifacts-note">
          Loaded directly from local PNG artifacts, not from the historical run list.
        </div>
      </div>

      <div className="featured-artifacts-grid">
        {ARTIFACTS.map((artifact) => (
          <ArtifactCard
            key={artifact.path}
            artifact={artifact}
            cacheToken={cacheToken}
          />
        ))}
      </div>
    </section>
  );
}
