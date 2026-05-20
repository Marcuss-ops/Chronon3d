import { API_BASE } from '../data/constants.js';

export const outputPathToArtifactUrl = (outputPath, cacheBuster = '') => {
  if (!outputPath) return '';
  const url = new URL(`${API_BASE}/artifact`);
  url.searchParams.set('path', outputPath);
  if (cacheBuster) {
    url.searchParams.set('v', String(cacheBuster));
  }
  return url.toString();
};

export const isVideoOutput = (outputPath) => /\.(mp4|webm|mov)$/i.test(outputPath || '');
export const isImageOutput = (outputPath) => /\.(png|jpg|jpeg|webp|gif|svg)$/i.test(outputPath || '');

export const fetchRuns = async () => {
  const res = await fetch(`${API_BASE}/api/runs`);
  if (!res.ok) throw new Error('Failed to load runs');
  return await res.json();
};

export const fetchRunDetail = async (id) => {
  const res = await fetch(`${API_BASE}/api/run/${id}`);
  if (!res.ok) throw new Error('Failed to load run details');
  return await res.json();
};
