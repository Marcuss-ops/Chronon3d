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
