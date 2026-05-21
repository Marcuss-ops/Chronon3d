import { API_BASE } from '../data/constants.js';

function getAuthHeaders() {
  const token = localStorage.getItem('chronon_auth_token');
  return token ? { 'Authorization': `Bearer ${token}` } : {};
}

export const login = async (password) => {
  const res = await fetch(`${API_BASE}/api/login`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ password })
  });
  if (!res.ok) {
    const data = await res.json().catch(() => ({}));
    throw new Error(data.error || 'Login failed');
  }
  return await res.json();
};

export const logout = async () => {
  const token = localStorage.getItem('chronon_auth_token');
  if (token) {
    await fetch(`${API_BASE}/api/logout`, {
      method: 'POST',
      headers: { 'Authorization': `Bearer ${token}` }
    });
  }
  localStorage.removeItem('chronon_auth_token');
};

export const outputPathToArtifactUrl = (outputPath, cacheBuster = '') => {
  if (!outputPath) return '';
  const url = new URL(`${API_BASE}/artifact`);
  url.searchParams.set('path', outputPath);
  const token = localStorage.getItem('chronon_auth_token');
  if (token) {
    url.searchParams.set('token', token);
  }
  if (cacheBuster) {
    url.searchParams.set('v', String(cacheBuster));
  }
  return url.toString();
};

export const isVideoOutput = (outputPath) => /\.(mp4|webm|mov)$/i.test(outputPath || '');
export const isImageOutput = (outputPath) => /\.(png|jpg|jpeg|webp|gif|svg)$/i.test(outputPath || '');

export const fetchRuns = async () => {
  const res = await fetch(`${API_BASE}/api/runs`, { headers: getAuthHeaders() });
  if (!res.ok) throw new Error('Failed to load runs');
  return await res.json();
};

export const fetchRunDetail = async (id) => {
  const res = await fetch(`${API_BASE}/api/run/${id}`, { headers: getAuthHeaders() });
  if (!res.ok) throw new Error('Failed to load run details');
  return await res.json();
};
