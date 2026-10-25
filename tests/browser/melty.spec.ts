import { expect, test } from '@playwright/test';
import { readFileSync } from 'node:fs';

const handoffs = JSON.parse(readFileSync('tests/fixtures/core.json', 'utf8')).cases as Array<{
  name: string; source: string; prefix: string; operation: string;
}>;

for (const family of ['b', 'c', 'd']) test(`Melty ${family.toUpperCase()} supports independent opposite caps, exact angles and saved history`, async ({ page }) => {
  test.setTimeout(120_000);
  const id = `melty-${family}`;
  const fixture = handoffs.find(item => item.source === `packages/${id}/package.json` && item.name.endsWith('opposite caps execute separately'))!;
  const errors: string[] = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.goto('./');
  await expect(page.getByTestId('state-digest')).toHaveText(/^[0-9a-f]{64}$/);
  await page.getByLabel('Puzzle', { exact: true }).selectOption(id);
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-realization-id', `${id}-euclidean-v1`, { timeout: 30_000 });
  await expect(page.getByTestId('diagram-view')).toHaveAttribute('data-realization-id', `${id}-port-diagram-v1`);
  await page.getByLabel('Animation speed').selectOption('0');
  await page.getByText('Advanced movement list', { exact: true }).click();
  await expect(page.getByRole('button', { name: 'Move D', exact: true })).toHaveAttribute('title', family === 'c' ? '89.709°' : '85.402°');
  const initial = await page.getByTestId('state-digest').textContent();
  await page.getByRole('button', { name: 'Select corner/URF', exact: true }).click();
  for (const view of ['cube-view', 'diagram-view']) {
    await expect(page.getByTestId(view)).toHaveAttribute('data-destination-piece', 'corner/URF');
    await expect(page.getByTestId(view)).not.toHaveAttribute('data-destination-count', '0');
  }
  await page.keyboard.press('Escape');
  await page.getByLabel('Algorithm', { exact: true }).fill(fixture.prefix);
  await page.getByRole('button', { name: 'Play algorithm →' }).click();
  await expect(page.getByTestId('state-digest')).not.toHaveText(initial!);
  await expect(page.getByTestId('app')).toHaveAttribute('data-animating', 'false');
  const beforeCap = await page.getByTestId('state-digest').textContent();
  await page.getByRole('button', { name: `Move ${fixture.operation}`, exact: true }).click();
  await expect(page.getByTestId('state-digest')).not.toHaveText(beforeCap!);
  await expect(page.getByTestId('app')).toHaveAttribute('data-animating', 'false');
  const completed = await page.getByTestId('state-digest').textContent();
  const downloading = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Save session ↗' }).click();
  const saved = await (await downloading).path();
  await page.getByRole('button', { name: '↶ Undo', exact: true }).click();
  await expect(page.getByTestId('state-digest')).toHaveText(beforeCap!);
  await page.getByRole('button', { name: '↷ Redo', exact: true }).click();
  await expect(page.getByTestId('state-digest')).toHaveText(completed!);
  await page.getByRole('button', { name: '↶ Undo', exact: true }).click();
  await page.getByLabel('Open session file').setInputFiles(saved!);
  await expect(page.getByTestId('state-digest')).toHaveText(completed!);
  for (const view of ['cube-view', 'diagram-view'])
    await expect(page.getByTestId(view)).toHaveAttribute('data-state-digest', completed!);
  await expect(page.getByRole('alert')).toHaveCount(0);
  expect(errors).toEqual([]);
  await page.screenshot({ path: `test-results/${id}-independent.png`, fullPage: true });
});

test('Melty selections refresh endpoint ghosts after rapid selection and state changes', async ({ page }) => {
  test.setTimeout(120_000);
  await page.goto('./');
  await expect(page.getByTestId('state-digest')).toHaveText(/^[0-9a-f]{64}$/);
  await page.getByLabel('Puzzle', { exact: true }).selectOption('melty-a');
  const cube = page.getByTestId('cube-view');
  await expect(cube).toHaveAttribute('data-realization-id', 'melty-a-euclidean-v1', { timeout: 30_000 });
  await page.getByLabel('Animation speed').selectOption('0');
  await page.getByText('Advanced movement list', { exact: true }).click();
  const initial = await page.getByTestId('state-digest').textContent();
  await page.getByRole('button', { name: 'Select corner/URF', exact: true }).evaluate(button => {
    (button as HTMLButtonElement).click();
    requestAnimationFrame(() => document.querySelector<HTMLButtonElement>('[aria-label="Select edge/UF"]')!.click());
  });
  for (const id of ['cube-view', 'diagram-view']) {
    await expect(page.getByTestId(id)).toHaveAttribute('data-selected-piece', 'edge/UF');
    await expect(page.getByTestId(id)).toHaveAttribute('data-destination-piece', 'edge/UF');
  }
  await page.getByRole('button', { name: 'Select corner/URF', exact: true }).click();
  await expect(cube).toHaveAttribute('data-selected-piece', 'corner/URF');
  await page.getByRole('button', { name: 'Move U', exact: true }).click();
  await expect(page.getByTestId('state-digest')).not.toHaveText(initial!);
  await expect(page.getByTestId('app')).toHaveAttribute('data-animating', 'false');
  await expect(cube).toHaveAttribute('data-destination-piece', 'corner/URF');
  await cube.locator('.destination-controls details summary').click();
  await cube.getByRole('button', { name: 'Twist U_inv', exact: true }).click();
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  await page.keyboard.press('Escape');
  for (const id of ['cube-view', 'diagram-view'])
    await expect(page.getByTestId(id)).toHaveAttribute('data-destination-count', '0');
  await expect(page.getByRole('alert')).toHaveCount(0);
});

test('Melty A supports shape changes, independent cap stops, history and saved sessions', async ({ page }) => {
  test.setTimeout(120_000);
  const errors: string[] = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.goto('./');
  await expect(page.getByTestId('state-digest')).toHaveText(/^[0-9a-f]{64}$/);
  await page.getByLabel('Puzzle', { exact: true }).selectOption('melty-a');
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-realization-id', 'melty-a-euclidean-v1', { timeout: 30_000 });
  await expect(page.getByTestId('diagram-view')).toHaveAttribute('data-realization-id', 'melty-a-port-diagram-v1');
  await expect(page.locator('canvas')).toHaveCount(2);
  await page.getByLabel('Animation speed').selectOption('0');
  await page.getByText('Advanced movement list', { exact: true }).click();
  const initial = await page.getByTestId('state-digest').textContent();
  await expect(page.getByRole('button', { name: 'Move U', exact: true })).toHaveAttribute('title', '85.402°');
  await page.getByRole('button', { name: 'Move U', exact: true }).click();
  await expect(page.getByTestId('state-digest')).not.toHaveText(initial!);
  await page.getByRole('button', { name: '↶ Undo', exact: true }).click();
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  await page.getByLabel('Algorithm', { exact: true }).fill('U_e_inv R_e U_e R_e_inv');
  await page.getByRole('button', { name: 'Play algorithm →' }).click();
  await expect(page.getByTestId('state-digest')).not.toHaveText(initial!);
  await expect(page.getByTestId('app')).toHaveAttribute('data-animating', 'false');
  const beforeCap = await page.getByTestId('state-digest').textContent();
  const partial = page.getByRole('button', { name: 'Move U_c', exact: true });
  await partial.click();
  await expect(page.getByTestId('state-digest')).not.toHaveText(beforeCap!);
  const intermediate = await page.getByTestId('state-digest').textContent();
  await page.getByRole('button', { name: 'Move D_b_inv', exact: true }).click();
  await expect(page.getByTestId('state-digest')).not.toHaveText(intermediate!);
  const completed = await page.getByTestId('state-digest').textContent();
  const download = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Save session ↗' }).click();
  const saved = (await download);
  await page.getByRole('button', { name: '↶ Undo', exact: true }).click();
  await expect(page.getByTestId('state-digest')).toHaveText(intermediate!);
  await page.getByLabel('Open session file').setInputFiles((await saved.path())!);
  await expect(page.getByTestId('state-digest')).toHaveText(completed!);
  for (const id of ['cube-view', 'diagram-view'])
    await expect(page.getByTestId(id)).toHaveAttribute('data-state-digest', completed!);
  await expect(page.getByRole('alert')).toHaveCount(0);
  await page.screenshot({ path: 'test-results/melty-a-independent.png', fullPage: true });
  expect(errors).toEqual([]);
});
