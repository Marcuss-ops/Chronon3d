import { API_BASE } from '../data/constants.js';

async function handleApiResponse(res, defaultError) {
  if (res.ok) {
    return await res.json();
  }
  const data = await res.json().catch(() => ({}));
  throw new Error(data.error || defaultError);
}

export const outputPathToArtifactUrl = (outputPath) => {
  if (!outputPath) return '';
  const base = API_BASE || window.location.origin;
  const url = new URL(`${base}/artifact`);
  url.searchParams.set('path', outputPath);
  return url.toString();
};

export const isVideoOutput = (outputPath) => /\.(mp4|webm|mov)$/i.test(outputPath || '');
export const isImageOutput = (outputPath) => /\.(png|jpg|jpeg|webp|gif|svg)$/i.test(outputPath || '');

export const fetchRuns = async () => {
  const res = await fetch(`${API_BASE}/api/runs`);
  return await handleApiResponse(res, 'Failed to load runs');
};

export const fetchRunDetail = async (id) => {
  const res = await fetch(`${API_BASE}/api/run/${id}`);
  return await handleApiResponse(res, 'Failed to load run details');
};
