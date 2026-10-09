import { defineConfig } from '@playwright/test';

const basePath = `${(process.env.SITE_BASE_PATH || '/').replace(/\/+$/, '')}/`;
const baseURL = `http://127.0.0.1:4173${basePath}`;

export default defineConfig({
  testDir: './tests/browser',
  workers: 1,
  fullyParallel: false,
  use: {
    baseURL,
    viewport: { width: 1440, height: 1000 },
    launchOptions: { args: ['--enable-unsafe-swiftshader', '--disable-dev-shm-usage'] },
  },
  webServer: {
    // Keep the test server to one Node process under the container memory budget.
    command: 'node node_modules/vite/bin/vite.js preview --host 0.0.0.0',
    url: baseURL,
    reuseExistingServer: !process.env.CI,
    timeout: 120_000,
  },
});
