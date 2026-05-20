import './styles/base.css';
import './styles/sidebar.css';
import './styles/metrics.css';
import './styles/profiling.css';
import './styles/tabs.css';
import './styles/preview.css';
import React, { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import './index.css'
import App from './App.jsx'

createRoot(document.getElementById('root')).render(
  <StrictMode>
    <App />
  </StrictMode>,
)
