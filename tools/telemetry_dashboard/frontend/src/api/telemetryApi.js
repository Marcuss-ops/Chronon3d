import { API_BASE } from '../data/constants.js';

export const outputPathToArtifactUrl = (outputPath) => {
  if (!outputPath) return '';
  return `${API_BASE}/artifact?path=${encodeURIComponent(outputPath)}`;
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
