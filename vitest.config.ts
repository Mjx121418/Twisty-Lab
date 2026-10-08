import { defineConfig } from 'vitest/config';

export default defineConfig({
  test: {
    include: ['tests/unit/**/*.test.ts'],
    maxWorkers: 1,
    fileParallelism: false,
    testTimeout: 30_000,
  },
});
