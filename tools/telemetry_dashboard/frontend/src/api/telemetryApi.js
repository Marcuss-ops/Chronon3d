import { API_BASE } from '../data/constants.js';

async function handleApiResponse(res, defaultError) {
  if (res.ok) {
    return await res.json();
  }
  const data = await res.json().catch(() => ({}));
  throw new Error(data.error || defaultError);
}

export const outputPathToArtifactUrl = (outputPath, version = '') => {
  if (!outputPath) return '';
  const base = API_BASE || window.location.origin;
  const url = new URL(`${base}/artifact`);
  url.searchParams.set('path', outputPath);
  if (version) {
    url.searchParams.set('v', version);
  }
  return url.toString();
};

export const isVideoOutput = (outputPath) => /\.(mp4|webm|mov)$/i.test(outputPath || '');
export const isImageOutput = (outputPath) => /\.(png|jpg|jpeg|webp|gif|svg)$/i.test(outputPath || '');

/**
 * Build a URLSearchParams from a plain object, dropping undefined/null/empty.
 * `since`/`until` are passed through verbatim (ISO 8601 strings).
 */
function buildRunsQuery(params) {
  const query = new URLSearchParams();
  if (params && typeof params === 'object') {
    for (const [key, value] of Object.entries(params)) {
      if (value === undefined || value === null || value === '') continue;
      query.set(key, String(value));
    }
  }
  // Cache-buster: ensures vite dev server always re-forwards to backend.
  query.set('_', String(Date.now()));
  return query;
}

export const fetchRuns = async (params = {}) => {
  const query = buildRunsQuery(params);
  const res = await fetch(`${API_BASE}/api/runs?${query.toString()}`);
  return await handleApiResponse(res, 'Failed to load runs');
};

/**
 * Paged variant of fetchRuns that also returns the X-Total-Count header.
 * Use this when the caller needs to render pagination controls or display
 * "showing N of M" labels. The runs array shape is identical to fetchRuns.
 *
 * @param {object} params - { limit, offset, composition_id, run_id, success, since, until }
 * @returns {Promise<{ runs: object[], totalCount: number, limit: number, offset: number }>}
 */
export const fetchRunsPaged = async (params = {}) => {
  const query = buildRunsQuery(params);
  const res = await fetch(`${API_BASE}/api/runs?${query.toString()}`);
  if (!res.ok) {
    const data = await res.json().catch(() => ({}));
    throw new Error(data.error || 'Failed to load runs');
  }
  const runs = await res.json();
  const rawTotal = res.headers.get('X-Total-Count');
  const totalCount = rawTotal !== null ? parseInt(rawTotal, 10) || runs.length : runs.length;
  return {
    runs,
    totalCount,
    limit: (params && params.limit != null) ? Number(params.limit) : 50,
    offset: (params && params.offset != null) ? Number(params.offset) : 0,
  };
};

export const fetchRunDetail = async (id) => {
  const query = new URLSearchParams({ _: String(Date.now()) });
  const res = await fetch(`${API_BASE}/api/run/${id}?${query.toString()}`);
  return await handleApiResponse(res, 'Failed to load run details');
};
