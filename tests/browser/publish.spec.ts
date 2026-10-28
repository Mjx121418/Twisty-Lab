import { expect, test, type Page } from '@playwright/test';

// Check every shipped preset without native tools or exhaustive model audits.
const inventory = [
  ['tesseract3', 'Xp_fY'],
  ['cube3', 'R'],
  ['bandaged-uf-ufr', 'U'],
  ['helicopter', 'UF_ab'],
  ['bagua', 'U+'],
  ['melty-a', 'U'],
  ['melty-b', 'U'],
  ['melty-c', 'U'],
  ['melty-d', 'U'],
  ['icosahedron-110', 'A'],
  ['icosahedron-152', 'A'],
  ['gear', 'R'],
  ['gear-extreme', 'Uq'],
  ['gear-plain', 'Rq'],
  ['flat-torus', 'A'],
  ['flat-klein-bottle', 'A'],
  ['flat-hex-torus', 'A'],
  ['flat-hex-klein-bottle', 'A'],
  ['flat-pillowcase', 'A'],
  ['flat-pillowcase-6', 'E'],
  ['flat-333', 'A'],
  ['flat-333-6', 'D'],
] as const;

// A bounded workspace can run disjoint batches in fresh browser processes.
// The default publish action still checks the complete inventory.
const selected = process.env.PUBLISH_PRESETS?.split(',').filter(Boolean);
if (selected?.some(id => !inventory.some(([puzzle]) => puzzle === id))) throw new Error('Unknown PUBLISH_PRESETS selection');
const presets = selected ? inventory.filter(([id]) => selected.includes(id)) : inventory;

const loadTimeout = 30_000;
const presetTimeout = 60_000;
// Keep the switching sequence in one page, with time for every selected preset.
// A slow or stuck preset still fails within its own bounded step.
test.describe.configure({ timeout: loadTimeout + presets.length * presetTimeout });
test.use({ actionTimeout: loadTimeout });

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
  for (const view of await page.locator('[data-realization-id]').all())
    await expect(view).toHaveAttribute('data-state-digest', digest!);
  await expect(page.getByRole('alert')).toHaveCount(0);
}

test('published simulator loads every preset and keeps both views working through move and undo', async ({ page }) => {
  const errors = collectErrors(page);
  await page.goto('./');
  await expect(page.getByTestId('state-digest')).toHaveText(/^[0-9a-f]{64}$/, { timeout: loadTimeout });
  await page.getByLabel('Animation speed').selectOption('0');
  for (const [puzzle, move] of presets) {
    await test.step(puzzle, async () => {
      await page.getByLabel('Puzzle', { exact: true }).selectOption(puzzle);
      await expect(page.locator('canvas')).toHaveCount(puzzle === 'tesseract3' ? 1 : 2, { timeout: loadTimeout });
      await resting(page);
      const initial = await page.getByTestId('state-digest').textContent();
      const movement = page.getByRole('button', { name: `Move ${move}`, exact: true });
      const advanced = page.getByText('Advanced movement list', { exact: true });
      if (await advanced.isVisible() && !await movement.isVisible()) await advanced.click();
      await movement.click();
      await expect(page.getByTestId('state-digest')).not.toHaveText(initial!);
      await resting(page);
      await page.getByRole('button', { name: '↶ Undo', exact: true }).click();
      await expect(page.getByTestId('state-digest')).toHaveText(initial!);
      await resting(page);
    }, { timeout: presetTimeout });
  }
  expect(errors).toEqual([]);
});

test('published standalone SDK loads its modules and puzzle data and supports move and undo', async ({ page, baseURL }) => {
  const errors = collectErrors(page);
  const modules: string[] = [];
  page.on('request', (request) => {
    if (/\.(js|mjs)(\?|$)/.test(request.url())) modules.push(request.url());
  });
  await page.goto('kernel/examples/canvas/');
  await expect(page.getByRole('status')).toHaveText('Ready', { timeout: loadTimeout });
  await expect(page.locator('canvas')).toHaveCount(1);
  for (const [puzzle, move] of presets) {
    await test.step(puzzle, async () => {
      await page.getByLabel('Puzzle', { exact: true }).selectOption(puzzle === 'bandaged-uf-ufr' ? 'bandaged' : puzzle);
      await expect(page.getByRole('status')).toHaveText('Ready', { timeout: loadTimeout });
      await expect(page.locator('#digest')).toHaveText(/^[0-9a-f]{64}$/);
      const initial = await page.locator('#digest').textContent();
      await page.getByRole('button', { name: move, exact: true }).click();
      await expect(page.locator('#digest')).not.toHaveText(initial!);
      await page.getByRole('button', { name: 'Undo', exact: true }).click();
      await expect(page.locator('#digest')).toHaveText(initial!);
    }, { timeout: presetTimeout });
  }
  const kernelPath = new URL('kernel/', baseURL!).pathname;
  expect(modules.some((url) => new URL(url).pathname === `${kernelPath}index.js`)).toBe(true);
  expect(modules.every((url) => new URL(url).pathname.startsWith(kernelPath))).toBe(true);
  expect(errors).toEqual([]);
});
