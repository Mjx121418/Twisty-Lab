import { test, expect, type Page } from '@playwright/test';
import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';

async function resting(page: Page): Promise<void> {
  await expect(page.getByTestId('app')).toHaveAttribute('data-animating', 'false');
  const digest = await page.getByTestId('state-digest').textContent();
  for (const id of ['cube-view', 'diagram-view']) await expect(page.getByTestId(id)).toHaveAttribute('data-state-digest', digest!);
}
test.beforeEach(async ({ page }) => {
  await page.goto('./');
  await expect(page.getByTestId('state-digest')).toHaveText(/[0-9a-f]{64}/);
  await page.getByLabel('Puzzle', { exact: true }).selectOption('gear');
  await expect(page.locator('canvas')).toHaveCount(2);
  await expect(page.getByLabel('Gear edge UF')).toBeChecked();
});

test('animates coupled gears in synchronized views with shared selection and history', async ({ page }) => {
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-realization-id', 'gear-normal-euclidean-v1');
  await expect(page.getByLabel('Gear edge model')).toHaveCount(0);
  const initial = await page.getByTestId('state-digest').textContent();
  await page.getByRole('button', { name: 'Select edge/UF', exact: true }).click();
  for (const id of ['cube-view', 'diagram-view']) await expect(page.getByTestId(id)).toHaveAttribute('data-selected-piece', 'edge/UF');
  await page.getByRole('button', { name: 'Move Rq', exact: true }).click();
  await expect(page.getByTestId('blocking-evidence')).toContainText('placement.blocked');
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  await page.getByRole('button', { name: 'Move R', exact: true }).click();
  await resting(page);
  const native = JSON.parse(execFileSync('build/native/twisty', ['run', '--definition', 'packages/gear/source.json', '--algorithm', 'R', '--json'], { encoding: 'utf8' }));
  await expect(page.getByTestId('state-digest')).toHaveText(native.snapshot.stateDigest);
  for (const id of ['cube-view', 'diagram-view']) await expect(page.getByTestId(id)).toHaveAttribute('data-selected-piece', 'edge/UF');
  await page.getByRole('button', { name: '↶ Undo' }).click(); await resting(page);
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  await page.getByLabel('Algorithm', { exact: true }).fill('R12');
  await page.getByLabel('Animation speed').selectOption('0');
  await page.getByRole('button', { name: 'Play algorithm →' }).click(); await resting(page);
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  await expect(page.getByRole('alert')).toHaveCount(0);
  await page.screenshot({ path: 'test-results/gear-cube.png', fullPage: true });
});

test('configures arbitrary edge masks and enables ordinary turns on the plain slice', async ({ page }) => {
  await page.getByLabel('Gear edge UF').uncheck();
  await expect(page.getByLabel('Puzzle', { exact: true })).toHaveValue('gear-custom');
  await expect(page.getByLabel('Gear edge UF')).not.toBeChecked();
  await page.getByRole('button', { name: 'Move R', exact: true }).click();
  await expect(page.getByTestId('blocking-evidence')).toContainText('placement.blocked');
  await page.getByLabel('Puzzle', { exact: true }).selectOption('gear-extreme');
  await expect(page.getByLabel('Gear edge FR')).not.toBeChecked();
  await expect(page.getByLabel('Gear edge UF')).toBeChecked();
  await page.getByTestId('cube-view').click({ position: { x: 4, y: 4 } });
  await page.keyboard.press('u'); await resting(page);
  const native = JSON.parse(execFileSync('build/native/twisty', ['run', '--definition', 'packages/gear-extreme/source.json', '--algorithm', 'Uq', '--json'], { encoding: 'utf8' }));
  await expect(page.getByTestId('state-digest')).toHaveText(native.snapshot.stateDigest);
  await expect(page.getByRole('alert')).toHaveCount(0);
});

test('switches edge models without resetting state and restores the saved model', async ({ page }) => {
  // Normal Gear Cube uses the supplied model; legacy alternatives remain on Extreme.
  await page.getByLabel('Puzzle', { exact: true }).selectOption('gear-extreme');
  const initial = await page.getByTestId('state-digest').textContent();
  await page.getByRole('button', { name: 'Select edge/UF', exact: true }).click();
  await page.getByRole('button', { name: 'Move R', exact: true }).click(); await resting(page);
  const turned = await page.getByTestId('state-digest').textContent();
  await page.getByLabel('Gear edge model').selectOption('6');
  for (const id of ['cube-view', 'diagram-view']) {
    await expect(page.getByTestId(id)).toHaveAttribute('data-realization-id', /six-teeth/);
    await expect(page.getByTestId(id)).toHaveAttribute('data-selected-piece', 'edge/UF');
  }
  await resting(page);
  await expect(page.getByTestId('state-digest')).toHaveText(turned!);
  await page.screenshot({ path: 'test-results/gear-six-teeth-R.png', fullPage: true });
  const downloading = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Save session ↗' }).click();
  const saved = readFileSync((await (await downloading).path())!, 'utf8');
  expect(JSON.parse(saved).presentation.gearModel).toBe('6');
  await page.getByLabel('Gear edge model').selectOption('12'); await resting(page);
  const downloadingTwelve = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Save session ↗' }).click();
  const twelve = JSON.parse(readFileSync((await (await downloadingTwelve).path())!, 'utf8'));
  expect(twelve.presentation.cameras).toEqual(JSON.parse(saved).presentation.cameras);
  await page.getByLabel('Open session file').setInputFiles({ name: 'gear-six.json', mimeType: 'application/json', buffer: Buffer.from(saved) });
  await expect(page.getByLabel('Gear edge model')).toHaveValue('6');
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-realization-id', /six-teeth/);
  await resting(page);
  await expect(page.getByTestId('state-digest')).toHaveText(turned!);
  await page.getByRole('button', { name: '↶ Undo' }).click(); await resting(page);
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  await page.getByLabel('Gear edge model').selectOption('12'); await resting(page);
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-realization-id', 'gear-extreme-articulated-euclidean-v1');
  await page.getByLabel('Gear edge model').selectOption('6');
  await page.getByLabel('Gear edge UF').uncheck();
  await expect(page.getByLabel('Puzzle', { exact: true })).toHaveValue('gear-custom');
  await expect(page.getByLabel('Gear edge model')).toHaveValue('6');
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-realization-id', /six-teeth/);
  await expect(page.getByRole('alert')).toHaveCount(0);
});
