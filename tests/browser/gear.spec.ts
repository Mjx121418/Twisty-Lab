import { setViews, expectViews } from './view-controls';
import { test, expect, type Page } from '@playwright/test';
import { execFileSync } from 'node:child_process';
import { readFileSync, writeFileSync } from 'node:fs';

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
  await expect(page.getByTestId('blocking-evidence')).toContainText('relation.rejected');
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

test('normal F advances UR in the outward cog direction and undo restores it', async ({ page }) => {
  await expectViews(page, 'cube-sphere');
  await expect(page.getByTestId('diagram-view')).toHaveAttribute('data-realization-id', 'gear-spherical-v1');
  await expect(page.getByLabel('Spherical puzzle', { exact: true })).toBeVisible();
  await page.getByRole('button', { name: 'Select edge/UR', exact: true }).click();
  const placement = page.locator('.piece-detail code');
  await expect(placement).toHaveText('gear-edge:U-R/p0');
  const initial = await page.getByTestId('state-digest').textContent();
  await page.getByRole('button', { name: 'Move F', exact: true }).click(); await resting(page);
  await expect(placement).toHaveText('gear-edge:U-R/p1');
  for (const edge of ['UR', 'UL', 'DR', 'DL']) {
    await page.getByRole('button', { name: `Select edge/${edge}`, exact: true }).click();
    await expect(placement).toHaveText(/\/p1$/);
  }
  await page.getByRole('button', { name: 'Select edge/UR', exact: true }).click();
  await page.screenshot({ path: 'test-results/gear-normal-F-UR.png', fullPage: true });
  await page.getByRole('button', { name: '↶ Undo' }).click(); await resting(page);
  await expect(placement).toHaveText('gear-edge:U-R/p0');
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  await page.getByRole('button', { name: 'Move F inverse', exact: true }).click(); await resting(page);
  await expect(placement).toHaveText('gear-edge:U-R/p5');
  await page.getByRole('button', { name: '↶ Undo' }).click(); await resting(page);
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  await expect(page.getByRole('alert')).toHaveCount(0);
});

test('configures arbitrary edge masks and enables ordinary turns on the plain slice', async ({ page }) => {
  await page.getByLabel('Gear edge UF').uncheck();
  await expect(page.getByLabel('Puzzle', { exact: true })).toHaveValue('gear-custom');
  await expect(page.getByLabel('Gear edge UF')).not.toBeChecked();
  const initial = await page.getByTestId('state-digest').textContent();
  await page.getByRole('button', { name: 'Move Rq', exact: true }).click();
  await expect(page.getByTestId('blocking-evidence')).toContainText('relation.rejected');
  await page.getByRole('button', { name: 'Move R', exact: true }).click();
  await resting(page);
  await expect(page.getByTestId('state-digest')).not.toHaveText(initial!);
  await page.getByLabel('Puzzle', { exact: true }).selectOption('gear-extreme');
  await expect(page.getByLabel('Gear edge model')).toHaveCount(0);
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-realization-id', 'gear-extreme-supplied-articulated-euclidean-v1');
  await expect(page.getByLabel('Gear edge FR')).not.toBeChecked();
  await expect(page.getByLabel('Gear edge UF')).toBeChecked();
  await page.screenshot({ path: 'test-results/gear-extreme-supplied.png', fullPage: true });
  await page.getByTestId('cube-view').click({ position: { x: 4, y: 4 } });
  await page.keyboard.press('u'); await resting(page);
  const native = JSON.parse(execFileSync('build/native/twisty', ['run', '--definition', 'packages/gear-extreme/source.json', '--algorithm', 'Uq', '--json'], { encoding: 'utf8' }));
  await expect(page.getByTestId('state-digest')).toHaveText(native.snapshot.stateDigest);
  await expect(page.getByRole('alert')).toHaveCount(0);
});

test('allows all axes with only UF and UR geared and chooses coupling from current placements', async ({ page }, testInfo) => {
  await page.getByLabel('Puzzle', { exact: true }).selectOption('gear-plain');
  await page.getByLabel('Gear edge UF').check();
  await page.getByLabel('Gear edge UR').check();
  await page.getByLabel('Animation speed').selectOption('0');
  for (const face of ['U', 'D', 'R', 'L', 'F', 'B']) {
    const request = face === 'U' || face === 'D' ? `${face}q` : face;
    const button = page.getByRole('button', { name: `Move ${request}`, exact: true });
    await expect(button).not.toHaveClass(/may-block/);
  }
  await page.getByRole('button', { name: 'Select edge/UF', exact: true }).click();
  const initial = await page.getByTestId('state-digest').textContent();
  const source = JSON.parse(readFileSync('packages/gear/source.json', 'utf8'));
  source.puzzleId = 'gear-custom-experimental-v1'; source.gearedEdges = ['UF', 'UR'];
  const sourcePath = testInfo.outputPath('uf-ur-source.json');
  writeFileSync(sourcePath, JSON.stringify(source));
  const requests: string[] = [];
  for (const face of ['F', 'U', 'R', 'D', 'L', 'B']) {
    // Exactly one of the two directed requests is legal at every axis.
    const candidates = page.getByRole('button', { name: new RegExp(`^Move ${face}q?$`) });
    const states = await candidates.evaluateAll((buttons) => buttons.map((button) => ({
      operation: button.getAttribute('aria-label')!.slice(5), legal: !button.classList.contains('may-block'),
    })));
    const legal = states.filter((state) => state.legal);
    expect(legal).toHaveLength(1);
    requests.push(legal[0].operation);
    await page.getByTestId('cube-view').click({ position: { x: 4, y: 4 } });
    await page.keyboard.press(face.toLowerCase()); await resting(page);
    const native = JSON.parse(execFileSync('build/native/twisty', ['run', '--definition', sourcePath, '--algorithm', requests.join(' '), '--json'], { encoding: 'utf8' }));
    await expect(page.getByTestId('state-digest')).toHaveText(native.snapshot.stateDigest);
  }
  await expect(page.getByTestId('state-digest')).not.toHaveText(initial!);
  const downloading = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Save session ↗' }).click();
  const saved = JSON.parse(readFileSync((await (await downloading).path())!, 'utf8'));
  expect(saved.logical.requests.map((request: { operation: string }) => request.operation)).toEqual(requests);
  for (let i = 0; i < requests.length; ++i) {
    await page.getByRole('button', { name: '↶ Undo' }).click(); await resting(page);
  }
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  await page.getByLabel('Gear edge UF').uncheck();
  await expect(page.getByLabel('Algorithm', { exact: true })).toHaveValue("Rq Rq'");
  await expect(page.getByRole('button', { name: 'Move Rq', exact: true })).not.toHaveClass(/may-block/);
  await page.getByRole('button', { name: 'Play algorithm →' }).click(); await resting(page);
  await expect(page.getByTestId('goal-status')).toHaveText('Solved');
  await expect(page.getByRole('alert')).toHaveCount(0);
});

test('uses the supplied rigid edge in custom masks and restores saved views and history', async ({ page }) => {
  await page.getByLabel('Gear edge UF').uncheck();
  await expect(page.getByLabel('Gear edge model')).toHaveCount(0);
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-realization-id', 'gear-custom-supplied-articulated-euclidean-v1');
  const initial = await page.getByTestId('state-digest').textContent();
  await page.getByRole('button', { name: 'Select edge/UF', exact: true }).click();
  await page.getByRole('button', { name: 'Move U', exact: true }).click(); await resting(page);
  await expect(page.getByTestId('state-digest')).not.toHaveText(initial!);
  for (const id of ['cube-view', 'diagram-view'])
    await expect(page.getByTestId(id)).toHaveAttribute('data-selected-piece', 'edge/UF');
  await setViews(page, 'sphere-diagram'); await resting(page);
  const downloading = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Save session ↗' }).click();
  const saved = readFileSync((await (await downloading).path())!, 'utf8');
  expect(JSON.parse(saved).presentation).not.toHaveProperty('gearModel');
  await setViews(page, 'cube-diagram'); await resting(page);
  await page.getByLabel('Open session file').setInputFiles({ name: 'gear-supplied.json', mimeType: 'application/json', buffer: Buffer.from(saved) });
  await resting(page);
  await expectViews(page, 'sphere-diagram');
  await expect(page.getByLabel('Gear edge model')).toHaveCount(0);
  await page.getByRole('button', { name: '↶ Undo' }).click(); await resting(page);
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  await expect(page.getByRole('alert')).toHaveCount(0);
});

test('R and L spin the single UF gear identically', async ({ page }) => {
  await page.getByLabel('Puzzle', { exact: true }).selectOption('gear-plain');
  await page.getByLabel('Gear edge UF').check();
  await page.getByLabel('Animation speed').selectOption('0');
  await page.getByRole('button', { name: 'Select edge/UF', exact: true }).click();
  const initial = await page.getByTestId('state-digest').textContent();
  await page.getByRole('button', { name: 'Move R', exact: true }).click(); await resting(page);
  const expected = await page.getByTestId('state-digest').textContent();
  await page.getByRole('button', { name: '↶ Undo' }).click(); await resting(page);
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  await page.getByRole('button', { name: 'Move L', exact: true }).click(); await resting(page);
  await expect(page.getByTestId('state-digest')).toHaveText(expected!);
  for (const id of ['cube-view', 'diagram-view'])
    await expect(page.getByTestId(id)).toHaveAttribute('data-selected-piece', 'edge/UF');
  await page.getByRole('button', { name: '↶ Undo' }).click(); await resting(page);
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  await expect(page.getByRole('alert')).toHaveCount(0);
});

test('uses spherical geared and plain parts across presets, custom masks and saved views', async ({ page }) => {
  await expectViews(page, 'cube-sphere');
  await expect(page.getByLabel('Spherical puzzle', { exact: true })).toBeVisible();
  await page.screenshot({ path: 'test-results/gear-spherical-home.png', fullPage: true });
  await page.getByLabel('Puzzle', { exact: true }).selectOption('gear-extreme');
  await expect(page.getByTestId('diagram-view')).toHaveAttribute('data-realization-id', 'gear-extreme-spherical-v1');
  await page.getByRole('button', { name: 'Select edge/FR', exact: true }).click();
  await expect(page.locator('.piece-detail')).toContainText('edge:F-R');
  const origin = await page.getByTestId('state-digest').textContent();
  await page.getByRole('button', { name: 'Move Uq', exact: true }).click(); await resting(page);
  await expect(page.getByTestId('state-digest')).not.toHaveText(origin!);
  await setViews(page, 'sphere-diagram'); await resting(page);
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-realization-id', 'gear-extreme-spherical-v1');
  await page.getByRole('button', { name: '↶ Undo' }).click(); await resting(page);
  await expect(page.getByTestId('state-digest')).toHaveText(origin!);
  await page.getByLabel('Gear edge FR').check();
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-realization-id', 'gear-custom-spherical-v1');
  await page.getByRole('button', { name: 'Select edge/FR', exact: true }).click();
  await expect(page.locator('.piece-detail code')).toHaveText('gear-edge:F-R/p0');
  await page.getByRole('button', { name: 'Move U', exact: true }).click(); await resting(page);
  await expect(page.locator('.piece-detail code')).toHaveText('gear-edge:F-R/p1');
  const turned = await page.getByTestId('state-digest').textContent();
  const downloading = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Save session ↗' }).click();
  const saved = readFileSync((await (await downloading).path())!, 'utf8');
  expect(JSON.parse(saved).presentation.visibleViews).toEqual(['sphere', 'diagram']);
  await setViews(page, 'cube-diagram');
  await page.getByLabel('Open session file').setInputFiles({ name: 'gear-sphere.json', mimeType: 'application/json', buffer: Buffer.from(saved) });
  await resting(page);
  await expectViews(page, 'sphere-diagram');
  await expect(page.getByLabel('Spherical puzzle', { exact: true })).toBeVisible();
  await expect(page.getByTestId('state-digest')).toHaveText(turned!);
  await page.getByLabel('Puzzle', { exact: true }).selectOption('gear-plain'); await resting(page);
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-realization-id', 'gear-plain-spherical-v1');
  await expect(page.getByRole('alert')).toHaveCount(0);
});
