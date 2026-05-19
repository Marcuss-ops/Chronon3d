import React, { useState } from 'react';
import { QUERY_PRESETS } from '../data/constants.js';
import { executeQuery } from '../api/telemetryApi.js';

export default function QueryConsole({ initialQuery }) {
  const [customQuery, setCustomQuery] = useState(initialQuery || QUERY_PRESETS[0].query);
  const [queryResults, setQueryResults] = useState(null);
  const [queryError, setQueryError] = useState('');
  const [queryExecuting, setQueryExecuting] = useState(false);
  const [activePreset, setActivePreset] = useState(0);

  const handleExecuteQuery = async (queryText) => {
    setQueryExecuting(true);
    setQueryError('');
    try {
      const data = await executeQuery(queryText);
      setQueryResults(data);
    } catch (err) {
      setQueryError(err.message);
      setQueryResults(null);
    } finally {
      setQueryExecuting(false);
    }
  };

  // Auto-execute on first render when initialQuery is provided
  React.useEffect(() => {
    if (customQuery) {
      handleExecuteQuery(customQuery);
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  return (
    <section className="glass-panel query-panel animate-fade-in">
      <div className="panel-title">
        <span>💻 Advanced SQLite Query Diagnostics</span>
        <span style={{ fontSize: '0.8rem', color: 'var(--text-muted)' }}>Query the active telemetry run database directly</span>
      </div>

      <div className="query-presets">
        {QUERY_PRESETS.map((preset, idx) => (
          <button
            key={idx}
            className={`preset-btn ${activePreset === idx ? 'active' : ''}`}
            onClick={() => {
              setActivePreset(idx);
              setCustomQuery(preset.query);
            }}
          >
            {preset.name}
          </button>
        ))}
      </div>

      <div className="query-editor-container">
        <textarea
          className="query-textarea"
          value={customQuery}
          onChange={(e) => {
            setCustomQuery(e.target.value);
            setActivePreset(-1);
          }}
          placeholder="SELECT * FROM render_runs LIMIT 10;"
        />
        <div className="query-actions">
          <button
            className="execute-btn"
            disabled={queryExecuting}
            onClick={() => handleExecuteQuery(customQuery)}
          >
            {queryExecuting ? 'Executing...' : '⚡ Run Diagnostic Query'}
          </button>
        </div>
      </div>

      {queryError && (
        <div className="error-banner" style={{ marginTop: '12px', background: 'rgba(239,68,68,0.1)', border: '1px solid var(--color-danger)', color: 'var(--color-danger)', borderRadius: '8px', padding: '12px 16px', fontSize: '0.85rem' }}>
          <strong>Query Error:</strong> {queryError}
        </div>
      )}

      {queryResults && (
        <div className="results-container" style={{ marginTop: '12px' }}>
          <table className="results-table">
            <thead>
              <tr>
                {queryResults.length > 0 && Object.keys(queryResults[0]).map((key) => (
                  <th key={key}>{key}</th>
                ))}
              </tr>
            </thead>
            <tbody>
              {queryResults.map((row, idx) => (
                <tr key={idx}>
                  {Object.values(row).map((val, cellIdx) => (
                    <td key={cellIdx}>
                      {val === null ? (
                        <span style={{ color: 'var(--text-muted)', fontStyle: 'italic' }}>NULL</span>
                      ) : (
                        typeof val === 'number' && val % 1 !== 0 ? val.toFixed(3) : String(val)
                      )}
                    </td>
                  ))}
                </tr>
              ))}
              {queryResults.length === 0 && (
                <tr>
                  <td style={{ textAlign: 'center', padding: '20px', color: 'var(--text-muted)' }}>
                    Empty result set.
                  </td>
                </tr>
              )}
            </tbody>
          </table>
        </div>
      )}
    </section>
  );
}
