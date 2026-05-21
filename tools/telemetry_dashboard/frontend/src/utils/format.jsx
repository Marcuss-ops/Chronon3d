import { INFO_DESCRIPTIONS } from '../data/constants.js';

export const formatBytes = (bytes) => {
  if (bytes === null || bytes === undefined) return 'N/A';
  if (bytes === 0) return '0 B';
  if (bytes >= 1024 * 1024 * 1024) {
    return `${(bytes / (1024 * 1024 * 1024)).toFixed(2)} GB`;
  }
  if (bytes >= 1024 * 1024) {
    return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
  }
  if (bytes >= 1024) {
    return `${(bytes / 1024).toFixed(1)} KB`;
  }
  return `${bytes} B`;
};

export const formatIso = (isoStr) => {
  if (!isoStr) return '';
  const date = new Date(isoStr);
  return date.toLocaleString();
};

export const formatCounterValue = (name, val) => {
  if (typeof val !== 'number') return val;
  if (name.includes('bytes') || name.includes('memory') || name.includes('peak')) {
    return formatBytes(val);
  }
  if (val >= 1000000) {
    return `${(val / 1000000).toFixed(2)}M`;
  }
  if (val >= 1000) {
    return `${(val / 1000).toFixed(1)}K`;
  }
  return val.toLocaleString();
};

export const getFpsColor = (fps) => {
  if (!fps) return 'var(--text-primary)';
  if (fps < 1.0) return 'var(--color-danger)';
  if (fps < 5.0) return 'var(--color-warning)';
  return 'var(--color-success)';
};

export const getCacheHitColor = (rate) => {
  if (rate < 20.0) return 'var(--color-danger)';
  if (rate < 60.0) return 'var(--color-warning)';
  return 'var(--color-success)';
};

export const renderInfoIcon = (key) => {
  const desc = INFO_DESCRIPTIONS[key] || INFO_DESCRIPTIONS[key.replace(' ', '_')] || INFO_DESCRIPTIONS[key.replace(/ /g, '_')];
  if (!desc) return null;
  return (
    <span className="info-icon" title={desc}>
      ⓘ
    </span>
  );
};
