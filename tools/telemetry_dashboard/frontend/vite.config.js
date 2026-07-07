import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

const backendPort = process.env.VITE_BACKEND_PORT || '8000';
const backendTarget = `http://127.0.0.1:${backendPort}`;

// https://vite.dev/config/
export default defineConfig({
  plugins: [react()],
  server: {
    host: '0.0.0.0',
    port: 5173,
    strictPort: true,
    // TICKET-pubblico-dashboard: allow any Host header so the dashboard is
    // accessible via SSH-reverse-tunnel public URLs (e.g. serveo.net, which
    // uses random subdomains like `<random>-<ip>.serveousercontent.com`).
    // Vite 5+ rejects requests with non-localhost Host headers by default
    // (returns HTTP 403).  `true` = allow all (dev-server trade-off: the
    // server is now reachable from any Host header; this is acceptable for
    // a local dev dashboard but should be locked down for production).
    // For more restrictive access, set to a specific allowlist of hostnames
    // (e.g. `['.serveousercontent.com', 'localhost', '127.0.0.1']`).
    allowedHosts: true,
    proxy: {
      '/api': {
        target: backendTarget,
        changeOrigin: true,
      },
      '/artifact': {
        target: backendTarget,
        changeOrigin: true,
      },
      '/socket.io': {
        target: backendTarget,
        changeOrigin: true,
        ws: true,
      },
    },
  },
})
