// ── ErrorBoundary ─────────────────────────────────────────────────────────────────
// Cat-1 defensive: surfaces React mount-time / render-time errors visibly instead
// of silently leaving <div id="root"> empty (the symptom reported on :5173 and :8000
// post aad79a47). Wraps <App /> in main.jsx. Display is intentionally monospace +
// dark theme so dev debugging is immediate without DevTools.
//
// Lifecycle:
//   - render() throws → getDerivedStateFromError captures → render() returns
//     fallback UI with error.message + stack.
//   - Once in error state, the component never re-renders to its children
//     (avoid recursive failure loops). User can refresh to retry.

import React from 'react';

class ErrorBoundary extends React.Component {
  constructor(props) {
    super(props);
    this.state = { hasError: false, error: null, errorInfo: null };
  }

  static getDerivedStateFromError(error) {
    return { hasError: true, error };
  }

  componentDidCatch(error, errorInfo) {
    // eslint-disable-next-line no-console
    console.error('[ErrorBoundary] caught render error:', error, errorInfo);
    this.setState({ errorInfo });
  }

  render() {
    if (this.state.hasError) {
      const errMsg = (this.state.error && this.state.error.message) || 'Unknown error';
      const stack = (this.state.error && this.state.error.stack) || '(no stack trace)';
      const info = this.state.errorInfo && this.state.errorInfo.componentStack
        ? this.state.errorInfo.componentStack
        : '(no componentStack)';
      return (
        <div
          style={{
            padding: 24,
            fontFamily: 'ui-monospace, SFMono-Regular, Menlo, monospace',
            color: '#f0f6fc',
            background: '#0d1117',
            minHeight: '100vh',
            boxSizing: 'border-box',
          }}
        >
          <h1 style={{ color: '#f85149', marginTop: 0 }}>
            Dashboard failed to mount
          </h1>
          <p style={{ color: '#8b949e' }}>
            The React component tree threw during render. The error is shown below.
            Refreshing the page will retry the mount.
          </p>
          <h2 style={{ color: '#f0883e', marginBottom: 6 }}>Error</h2>
          <pre
            style={{
              background: '#161b22',
              padding: 16,
              borderRadius: 8,
              overflow: 'auto',
              whiteSpace: 'pre-wrap',
              wordBreak: 'break-word',
              margin: 0,
            }}
          >
            {errMsg}
          </pre>
          <h2 style={{ color: '#f0883e', marginBottom: 6 }}>Stack</h2>
          <pre
            style={{
              background: '#161b22',
              padding: 16,
              borderRadius: 8,
              overflow: 'auto',
              whiteSpace: 'pre-wrap',
              wordBreak: 'break-word',
              fontSize: 12,
            }}
          >
            {stack}
          </pre>
          <h2 style={{ color: '#f0883e', marginBottom: 6 }}>Component stack</h2>
          <pre
            style={{
              background: '#161b22',
              padding: 16,
              borderRadius: 8,
              overflow: 'auto',
              whiteSpace: 'pre-wrap',
              wordBreak: 'break-word',
              fontSize: 11,
            }}
          >
            {info}
          </pre>
        </div>
      );
    }
    return this.props.children;
  }
}

export default ErrorBoundary;
