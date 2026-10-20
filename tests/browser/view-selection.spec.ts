import { expect, test, type Page } from '@playwright/test';
import { readFileSync } from 'node:fs';
import { expectViews, setViews } from './view-controls';

async function save(page: Page) {
  const downloading = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Save session ↗' }).click();
  return JSON.parse(readFileSync((await (await downloading).path())!, 'utf8'));
}

function expectCamera(actual: { position: number[]; target: number[]; up?: number[]; zoom: number }, expected: typeof actual): void {
  expect(actual.zoom).toBeCloseTo(expected.zoom, 8);
  for (const key of ['position', 'target', 'up'] as const) {
    if (!expected[key]) continue;
    expected[key]!.forEach((value, index) => expect(actual[key]![index]).toBeCloseTo(value, 8));
  }
}

test('selects and orders all 3D views while preserving canvases, cameras, state and saved sessions', async ({ page }) => {
  test.setTimeout(90_000);
  const errors: string[] = []; page.on('pageerror', error => errors.push(error.message));
  await page.goto('./');
  await expect(page.getByTestId('state-digest')).toHaveText(/[0-9a-f]{64}/);
  await expectViews(page, ['cube', 'sphere']);
  await page.getByLabel('Animation speed').selectOption('0');
  const origin = await page.getByTestId('state-digest').textContent();
  const sphere = page.locator('[data-view-id="sphere"] canvas');
  const bounds = (await sphere.boundingBox())!;
  await page.mouse.move(bounds.x + bounds.width / 2, bounds.y + bounds.height / 2);
  await page.mouse.down();
  await page.mouse.move(bounds.x + bounds.width / 2 + 45, bounds.y + bounds.height / 2 + 25, { steps: 5 });
  await page.mouse.up();
  await sphere.evaluate(node => node.setAttribute('data-preserved', 'sphere'));
  const before = await save(page);
  await page.getByRole('button', { name: 'Select corner/UFR', exact: true }).click();
  await setViews(page, ['diagram', 'sphere', 'cube']);
  await expect(page.locator('.views canvas')).toHaveCount(3);
  await expect(sphere).toHaveAttribute('data-preserved', 'sphere');
  for (const host of await page.locator('.views .canvas-host').all()) {
    await expect(host).toHaveAttribute('data-selected-piece', 'corner/UFR');
    await expect(host).toHaveAttribute('data-destination-count', '6');
    await expect(host).toHaveAttribute('data-state-digest', origin!);
  }
  const reordered = await save(page);
  expectCamera(reordered.presentation.cameras[1], before.presentation.cameras[1]);
  expectCamera(reordered.presentation.cameras[2], before.presentation.cameras[0]);
  await page.screenshot({ path: 'test-results/three-views.png', fullPage: true });
  await page.getByRole('button', { name: 'Move U', exact: true }).click();
  await expect(page.getByTestId('state-digest')).not.toHaveText(origin!);
  const allViewsTurned = await page.getByTestId('state-digest').textContent();
  for (const host of await page.locator('.views .canvas-host').all()) await expect(host).toHaveAttribute('data-state-digest', allViewsTurned!);
  await page.getByRole('button', { name: '↶ Undo', exact: true }).click();
  await expect(page.getByTestId('state-digest')).toHaveText(origin!);

  await setViews(page, ['diagram']);
  await expect(page.locator('.views canvas')).toHaveCount(1);
  await page.locator('.view-picker summary').click();
  await expect(page.getByRole('checkbox', { name: 'Port diagram', exact: true })).toBeDisabled();
  await page.locator('.view-picker summary').click();
  await page.getByRole('button', { name: 'Move R', exact: true }).click();
  await expect(page.getByTestId('app')).toHaveAttribute('data-animating', 'false');
  await expect(page.getByTestId('state-digest')).not.toHaveText(origin!);
  const turned = await page.getByTestId('state-digest').textContent();
  await setViews(page, ['diagram', 'sphere']);
  const saved = await save(page);
  expect(saved.presentation.viewOrder).toEqual(['diagram', 'sphere', 'cube']);
  expect(saved.presentation.visibleViews).toEqual(['diagram', 'sphere']);
  expectCamera(saved.presentation.cameras[1], before.presentation.cameras[1]);
  expectCamera(saved.presentation.cameras[2], before.presentation.cameras[0]);

  await page.locator('.view-picker summary').click();
  await page.getByRole('button', { name: 'Restore default views', exact: true }).click();
  await page.locator('.view-picker summary').click();
  await expectViews(page, ['cube', 'sphere']);
  await page.getByLabel('Open session file').setInputFiles({ name: 'views.json', mimeType: 'application/json', buffer: Buffer.from(JSON.stringify(saved)) });
  await expectViews(page, ['diagram', 'sphere']);
  await expect(page.getByTestId('state-digest')).toHaveText(turned!);
  (await save(page)).presentation.cameras.forEach((camera: Parameters<typeof expectCamera>[0], index: number) => expectCamera(camera, saved.presentation.cameras[index]));

  // Old saves attached cameras to a pair, even when layout hid one member.
  const legacy = structuredClone(saved);
  delete legacy.presentation.viewOrder; delete legacy.presentation.visibleViews;
  legacy.presentation.viewPair = 'sphere-diagram'; legacy.presentation.layout = 'cube';
  legacy.presentation.cameras = [saved.presentation.cameras[1], saved.presentation.cameras[0]];
  await page.getByLabel('Open session file').setInputFiles({ name: 'legacy.json', mimeType: 'application/json', buffer: Buffer.from(JSON.stringify(legacy)) });
  await expectViews(page, ['sphere']);
  expectCamera((await save(page)).presentation.cameras[0], saved.presentation.cameras[1]);
  await page.locator('.view-picker summary').click();
  await page.screenshot({ path: 'test-results/view-selector.png', fullPage: true });
  await page.keyboard.press('Escape');
  await expect(page.locator('.view-picker')).not.toHaveAttribute('open');
  await expect(page.locator('[data-view-id="sphere"]')).toHaveAttribute('data-selected-piece', 'corner/UFR');
  await page.setViewportSize({ width: 390, height: 844 });
  await page.locator('.view-picker summary').click();
  const menu = (await page.locator('.view-picker fieldset').boundingBox())!;
  expect(menu.x).toBeGreaterThanOrEqual(0); expect(menu.x + menu.width).toBeLessThanOrEqual(390);
  await page.screenshot({ path: 'test-results/view-selector-mobile.png', fullPage: true });
  await expect(page.getByRole('alert')).toHaveCount(0); expect(errors).toEqual([]);
});

for (const puzzle of ['flat-torus', 'flat-hex-klein-bottle']) {
  test(`${puzzle}: offers only planar views with their original defaults`, async ({ page }) => {
    await page.goto('./');
    await expect(page.getByTestId('state-digest')).toHaveText(/[0-9a-f]{64}/);
    await page.getByLabel('Puzzle', { exact: true }).selectOption(puzzle);
    await expectViews(page, ['fundamental-domain', 'tiled-plane']);
    const origin = await page.getByTestId('state-digest').textContent();
    await page.locator('.view-picker summary').click();
    await expect(page.locator('.view-picker input')).toHaveCount(2);
    await expect(page.getByRole('checkbox', { name: 'Fundamental domain', exact: true })).toBeChecked();
    await expect(page.getByRole('checkbox', { name: 'Tiled plane', exact: true })).toBeChecked();
    await page.locator('.view-picker summary').click();
    await setViews(page, ['tiled-plane', 'fundamental-domain']);
    await expect(page.locator('[data-view-id="fundamental-domain"]')).toHaveAttribute('data-camera-zoom', '1');
    await setViews(page, ['fundamental-domain']);
    await expect(page.locator('.views canvas')).toHaveCount(1);
    await page.locator('.view-picker summary').click();
    await page.getByRole('button', { name: 'Restore default views', exact: true }).click();
    await page.locator('.view-picker summary').click();
    await expectViews(page, ['fundamental-domain', 'tiled-plane']);
    await expect(page.getByTestId('state-digest')).toHaveText(origin!);
    await expect(page.getByRole('alert')).toHaveCount(0);
  });
}
