import { expect, test, type Page } from '@playwright/test';

// Check every shipped preset without native tools or exhaustive model audits.
const presets = [
  ['cube3', 'R'],
  ['bandaged-uf-ufr', 'U'],
  ['helicopter', 'UF_ab'],
  ['bagua', 'U+'],
  ['gear', 'R'],
  ['gear-extreme', 'Uq'],
  ['gear-plain', 'Rq'],
  ['flat-torus', 'A'],
  ['flat-klein-bottle', 'A'],
  ['flat-hex-torus', 'A'],
  ['flat-hex-klein-bottle', 'A'],
] as const;

function collectErrors(page: Page): string[] {
  const errors: string[] = [];
  page.on('pageerror', (error) => errors.push(error.message));
  page.on('response', (response) => {
    if (response.status() >= 400) errors.push(`${response.status()} ${response.url()}`);
  });
  return errors;
}

async function resting(page: Page): Promise<void> {
  await expect(page.getByTestId('app')).toHaveAttribute('data-animating', 'false');
  const digest = await page.getByTestId('state-digest').textContent();
  for (const id of ['cube-view', 'diagram-view'])
    await expect(page.getByTestId(id)).toHaveAttribute('data-state-digest', digest!);
  await expect(page.getByRole('alert')).toHaveCount(0);
}

test('published simulator loads every preset and keeps both views working through move and undo', async ({ page }) => {
  test.setTimeout(120_000);
  const errors = collectErrors(page);
  await page.goto('./');
  await expect(page.getByTestId('state-digest')).toHaveText(/^[0-9a-f]{64}$/, { timeout: 15_000 });
  await page.getByLabel('Animation speed').selectOption('0');
  for (const [puzzle, move] of presets) {
    await test.step(puzzle, async () => {
      await page.getByLabel('Puzzle', { exact: true }).selectOption(puzzle);
      await expect(page.locator('canvas')).toHaveCount(2);
      await resting(page);
      const initial = await page.getByTestId('state-digest').textContent();
      await page.getByRole('button', { name: `Move ${move}`, exact: true }).click();
      await expect(page.getByTestId('state-digest')).not.toHaveText(initial!);
      await resting(page);
      await page.getByRole('button', { name: '↶ Undo', exact: true }).click();
      await expect(page.getByTestId('state-digest')).toHaveText(initial!);
      await resting(page);
    });
  }
  expect(errors).toEqual([]);
});

test('published standalone SDK loads its modules and puzzle data and supports move and undo', async ({ page, baseURL }) => {
  test.setTimeout(120_000);
  const errors = collectErrors(page);
  const modules: string[] = [];
  page.on('request', (request) => {
    if (/\.(js|mjs)(\?|$)/.test(request.url())) modules.push(request.url());
  });
  await page.goto('kernel/examples/canvas/');
  await expect(page.getByRole('status')).toHaveText('Ready', { timeout: 15_000 });
  await expect(page.locator('canvas')).toHaveCount(1);
  for (const [puzzle, move] of presets) {
    await test.step(puzzle, async () => {
      await page.getByLabel('Puzzle', { exact: true }).selectOption(puzzle === 'bandaged-uf-ufr' ? 'bandaged' : puzzle);
      await expect(page.getByRole('status')).toHaveText('Ready');
      await expect(page.locator('#digest')).toHaveText(/^[0-9a-f]{64}$/);
      const initial = await page.locator('#digest').textContent();
      await page.getByRole('button', { name: move, exact: true }).click();
      await expect(page.locator('#digest')).not.toHaveText(initial!);
      await page.getByRole('button', { name: 'Undo', exact: true }).click();
      await expect(page.locator('#digest')).toHaveText(initial!);
    });
  }
  const kernelPath = new URL('kernel/', baseURL!).pathname;
  expect(modules.some((url) => new URL(url).pathname === `${kernelPath}index.js`)).toBe(true);
  expect(modules.every((url) => new URL(url).pathname.startsWith(kernelPath))).toBe(true);
  expect(errors).toEqual([]);
});
