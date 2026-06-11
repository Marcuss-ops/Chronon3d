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
  const base = API_BASE || window.location.origin;
  const url = new URL(`${base}/artifact`);
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

export class AuthError extends Error {
  constructor(msg) { super(msg); this.name = 'AuthError'; }
}

export const fetchRuns = async () => {
  const res = await fetch(`${API_BASE}/api/runs`, { headers: getAuthHeaders() });
  if (res.status === 401) {
    localStorage.removeItem('chronon_auth_token');
    throw new AuthError('Session expired');
  }
  if (!res.ok) throw new Error('Failed to load runs');
  return await res.json();
};

export const fetchRunDetail = async (id) => {
  const res = await fetch(`${API_BASE}/api/run/${id}`, { headers: getAuthHeaders() });
  if (res.status === 401) {
    localStorage.removeItem('chronon_auth_token');
    throw new AuthError('Session expired');
  }
  if (!res.ok) throw new Error('Failed to load run details');
  return await res.json();
};
