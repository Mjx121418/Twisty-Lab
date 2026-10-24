import { existsSync, readFileSync } from 'node:fs';
import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig(({ isPreview }) => {
  const builtIndex = new URL('./dist/index.html', import.meta.url);
  // Preview serves an existing build, which may have been made for Pages.
  // Its asset URLs retain that build's base even when the env var is unset.
  const builtBase = isPreview && existsSync(builtIndex)
    ? readFileSync(builtIndex, 'utf8').match(/<script\b[^>]*\bsrc="([^"]*\/)assets\/[^"]+"/)?.[1]
    : undefined;
  return {
    // GitHub Pages project sites serve assets below /repository/.
    base: process.env.SITE_BASE_PATH || builtBase || '/',
    plugins: [react()],
    server: { port: 5173, strictPort: true },
    preview: { port: 4173, strictPort: true },
    build: { target: 'es2022' },
  };
});
