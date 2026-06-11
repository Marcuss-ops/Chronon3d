import { API_BASE } from '../data/constants.js';

export class AuthError extends Error {
  constructor(message) {
    super(message);
    this.name = 'AuthError';
  }
}

function getAuthHeaders() {
  const token = localStorage.getItem('chronon_auth_token');
  return token ? { 'Authorization': `Bearer ${token}` } : {};
}

async function handleApiResponse(res, defaultError) {
  if (res.ok) {
    return await res.json();
  }
  if (res.status === 401) {
    localStorage.removeItem('chronon_auth_token');
    throw new AuthError('Session expired — please log in again');
  }
  const data = await res.json().catch(() => ({}));
  throw new Error(data.error || defaultError);
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

export const fetchRuns = async () => {
  const res = await fetch(`${API_BASE}/api/runs`, { headers: getAuthHeaders() });
  return await handleApiResponse(res, 'Failed to load runs');
};

export const fetchRunDetail = async (id) => {
  const res = await fetch(`${API_BASE}/api/run/${id}`, { headers: getAuthHeaders() });
  return await handleApiResponse(res, 'Failed to load run details');
};
