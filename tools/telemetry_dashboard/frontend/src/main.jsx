import './styles/base.css';
import './styles/sidebar.css';
import './styles/metrics.css';
import './styles/profiling.css';
import './styles/tabs.css';
import './styles/preview.css';
import React from 'react'
import { createRoot } from 'react-dom/client'
import './index.css'
import App from './App.jsx'
import ErrorBoundary from './components/ErrorBoundary.jsx'

// Cat-1 defensive: ErrorBoundary wraps the entire App tree so any runtime
// mount/render error (missing import, undefined component, React 19 strict-mode
// fail, etc.) surfaces visibly instead of silently leaving <div id="root"> empty.
// This was the symptom user-reported on :5173 + :8000 post the aad79a47 fix
// chain: React mount exited cleanly but the tree never populated. With this
// boundary the actual exception becomes readable on screen + console.
createRoot(document.getElementById('root')).render(
  <ErrorBoundary>
    <App />
  </ErrorBoundary>
)
