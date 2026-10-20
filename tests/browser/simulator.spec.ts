import { setViews, expectViews } from './view-controls';
import { test, expect } from '@playwright/test';
import { readFileSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { PerspectiveCamera, Vector3 } from 'three';

test.beforeEach(async ({ page }) => {
  await page.goto('./');
  await expect(page.getByTestId('state-digest')).toHaveText(/[0-9a-f]{64}/, { timeout: 15_000 });
  await expect(page.locator('canvas')).toHaveCount(2);
  await expect(page.getByRole('alert')).toHaveCount(0);
});
async function resting(page: import('@playwright/test').Page): Promise<void> {
  await expect(page.getByTestId('app')).toHaveAttribute('data-animating', 'false');
  const digest = await page.getByTestId('state-digest').textContent();
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-state-digest', digest!);
  await expect(page.getByTestId('diagram-view')).toHaveAttribute('data-state-digest', digest!);
}

test('realizes Bagua turns, published pair cycles, blocking and saved sessions', async ({ page }) => {
  test.setTimeout(60_000);
  await page.getByLabel('Puzzle', { exact: true }).selectOption('bagua');
  await expect(page.getByRole('button', { name: 'Select corner/00', exact: true })).toBeVisible();
  await setViews(page, 'cube-diagram');
  await expect(page.locator('canvas')).toHaveCount(2);
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-realization-id', 'bagua-euclidean-v1');
  await expect(page.getByTestId('diagram-view')).toHaveAttribute('data-realization-id', 'bagua-port-diagram-v1');
  await page.getByLabel('Animation speed').selectOption('0');
  const initial = await page.getByTestId('state-digest').textContent();
  await page.getByRole('button', { name: 'Move U+', exact: true }).click();
  await resting(page);
  const quarter = JSON.parse(execFileSync('build/native/twisty', ['run', '--definition', 'packages/bagua/relational-package.json', '--algorithm', 'U+ R', '--json'], { encoding: 'utf8' }));
  // Face keys retain quarter turns; Shift resolves the declared inverse ID.
  await page.locator('h1').click();
  await page.keyboard.press('r');
  await resting(page);
  await expect(page.getByTestId('state-digest')).toHaveText(quarter.snapshot.stateDigest);
  await page.keyboard.press('Shift+R');
  await page.getByRole('button', { name: 'Move U-', exact: true }).click();
  await resting(page);
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  const algorithm = '[[U+ R2:U+],[R R+ L-:D2]]';
  await page.getByLabel('Algorithm', { exact: true }).fill(algorithm);
  await page.getByRole('button', { name: 'Play algorithm →' }).click();
  await resting(page);
  const reference = JSON.parse(execFileSync('build/native/twisty', ['run', '--definition', 'packages/bagua/relational-package.json', '--algorithm', algorithm, '--json'], { encoding: 'utf8' }));
  await expect(page.getByTestId('state-digest')).toHaveText(reference.snapshot.stateDigest);
  await page.getByRole('button', { name: 'Select kite-left/00', exact: true }).click();
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-selected-piece', 'kite-left/00');
  await expect(page.getByTestId('diagram-view')).toHaveAttribute('data-selected-piece', 'kite-left/00');
  await page.screenshot({ path: 'test-results/bagua-pair-cycle.png', fullPage: true });
  const downloading = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Save session ↗' }).click();
  const saved = readFileSync((await (await downloading).path())!, 'utf8');
  await page.getByLabel('Algorithm', { exact: true }).fill(`(${algorithm})'`);
  await page.getByRole('button', { name: 'Play algorithm →' }).click();
  await resting(page);
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  await page.getByLabel('Execution policy').selectOption('transactional');
  await page.getByLabel('Algorithm', { exact: true }).fill('U+ R F- U+');
  await page.getByRole('button', { name: 'Play algorithm →' }).click();
  await expect(page.getByTestId('blocking-evidence')).toContainText('relation.rejected');
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-blocked-pieces', /(?:^|,)corner\/07(?:,|$)/);
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  await page.getByLabel('Open session file').setInputFiles({ name: 'bagua-session.json', mimeType: 'application/json', buffer: Buffer.from(saved) });
  await expect(page.getByTestId('state-digest')).toHaveText(reference.snapshot.stateDigest);
  await resting(page);
  await expect(page.getByRole('alert')).toHaveCount(0);
});

test('realizes spherical Bagua jumbling, picking, inherited blockers and saved views', async ({ page }) => {
  test.setTimeout(60_000);
  await page.getByLabel('Puzzle', { exact: true }).selectOption('bagua');
  await expectViews(page, 'cube-sphere');
  const sphere = page.getByLabel('Spherical puzzle', { exact: true });
  await expect(sphere).toBeVisible();
  await expect(page.getByTestId('diagram-view')).toHaveAttribute('data-realization-id', 'bagua-spherical-v1');
  await page.getByLabel('Animation speed').selectOption('0');
  await resting(page);
  const initial = await page.getByTestId('state-digest').textContent();
  await page.screenshot({ path: 'test-results/bagua-spherical-solved.png', fullPage: true });
  const bounds = await sphere.boundingBox();
  await sphere.click({ position: { x: bounds!.width / 2, y: bounds!.height / 2 } });
  await expect(page.getByTestId('diagram-view')).toHaveAttribute('data-selected-piece', /^(corner|center|triangle-center|triangle-edge|sliver|kite-left|kite-right|edge)\//);
  const selected = await page.getByTestId('diagram-view').getAttribute('data-selected-piece');
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-selected-piece', selected!);
  const algorithm = 'U+ R F-';
  await page.getByLabel('Algorithm', { exact: true }).fill(algorithm);
  await page.getByRole('button', { name: 'Play algorithm →' }).click();
  await resting(page);
  const reference = JSON.parse(execFileSync('build/native/twisty', ['run', '--definition', 'packages/bagua/relational-package.json', '--algorithm', algorithm, '--json'], { encoding: 'utf8' }));
  await expect(page.getByTestId('state-digest')).toHaveText(reference.snapshot.stateDigest);
  await page.getByRole('button', { name: 'Move U+', exact: true }).click();
  await expect(page.getByTestId('blocking-evidence')).toContainText('relation.rejected');
  await expect(page.getByTestId('diagram-view')).toHaveAttribute('data-blocked-pieces', /(?:^|,)corner\/07(?:,|$)/);
  await expect(page.getByTestId('state-digest')).toHaveText(reference.snapshot.stateDigest);
  await setViews(page, 'sphere-diagram');
  await resting(page);
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-realization-id', 'bagua-spherical-v1');
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-selected-piece', selected!);
  await page.screenshot({ path: 'test-results/bagua-spherical-jumbled.png', fullPage: true });
  const downloading = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Save session ↗' }).click();
  const saved = readFileSync((await (await downloading).path())!, 'utf8');
  expect(JSON.parse(saved).presentation.visibleViews).toEqual(['sphere', 'diagram']);
  await page.getByLabel('Algorithm', { exact: true }).fill(`(${algorithm})'`);
  await page.getByRole('button', { name: 'Play algorithm →' }).click();
  await resting(page);
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  await setViews(page, 'cube-diagram');
  await page.getByLabel('Open session file').setInputFiles({ name: 'bagua-sphere.json', mimeType: 'application/json', buffer: Buffer.from(saved) });
  await expect(page.getByTestId('state-digest')).toHaveText(reference.snapshot.stateDigest);
  await expectViews(page, 'sphere-diagram');
  await resting(page);
  await expect(page.getByRole('alert')).toHaveCount(0);
});

test('runs the same cube on a sphere, preserves state across views, and restores presentation', async ({ page }) => {
  const initial = await page.getByTestId('state-digest').textContent();
  await expectViews(page, 'cube-sphere');
  const sphere = page.getByLabel('Spherical puzzle', { exact: true });
  await expect(sphere).toBeVisible();
  await expect(page.getByTestId('diagram-view')).toHaveAttribute('data-realization-id', 'cube3-cube-spherical-v1');
  await page.screenshot({ path: 'test-results/spherical-solved.png', fullPage: true });
  const bounds = await sphere.boundingBox();
  await sphere.click({ position: { x: bounds!.width / 2, y: bounds!.height / 2 } });
  await expect(page.getByTestId('diagram-view')).toHaveAttribute('data-selected-piece', /^(corner|edge|center)\//);
  const selected = await page.getByTestId('diagram-view').getAttribute('data-selected-piece');
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-selected-piece', selected!);
  await page.getByLabel('Algorithm', { exact: true }).fill('U R F D L B');
  await page.getByRole('button', { name: 'Play algorithm →' }).click();
  await resting(page);
  const reference = JSON.parse(execFileSync('build/native/twisty', ['run', '--algorithm', 'U R F D L B', '--json'], { encoding: 'utf8' }));
  await expect(page.getByTestId('state-digest')).toHaveText(reference.snapshot.stateDigest);
  await setViews(page, 'sphere-diagram');
  await expect(page.getByTestId('cube-view').getByLabel('Spherical puzzle')).toBeVisible();
  await expect(page.getByTestId('state-digest')).toHaveText(reference.snapshot.stateDigest);
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-selected-piece', selected!);
  await page.getByRole('button', { name: 'History', exact: true }).click();
  await expect(page.locator('.history-list')).toContainText('6 committed primitives');
  await page.screenshot({ path: 'test-results/spherical-mixed.png', fullPage: true });
  const downloading = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Save session ↗' }).click();
  const saved = readFileSync((await (await downloading).path())!, 'utf8');
  const document = JSON.parse(saved);
  expect(document.schemaVersion).toBe(2);
  expect(document.logical.requests.every((request: { choices?: string[] }) => request.choices?.length === 3)).toBe(true);
  expect(document.presentation.visibleViews).toEqual(['sphere', 'diagram']);
  document.presentation.cameras = [
    { position: [4, 3, 7], target: [0, 0, 0], zoom: 1.2 },
    { position: [2, 1, 15], target: [1.5, 0, 0], zoom: 1.3 },
  ];
  await setViews(page, 'cube-diagram');
  await page.getByLabel('Algorithm', { exact: true }).fill("(U R F D L B)'");
  await page.getByRole('button', { name: 'Play algorithm →' }).click();
  await resting(page);
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  await page.getByLabel('Open session file').setInputFiles({ name: 'sphere-session.json', mimeType: 'application/json', buffer: Buffer.from(JSON.stringify(document)) });
  await expect(page.getByTestId('state-digest')).toHaveText(reference.snapshot.stateDigest);
  await expectViews(page, 'sphere-diagram');
  await resting(page);
  const restoredDownload = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Save session ↗' }).click();
  const restored = JSON.parse(readFileSync((await (await restoredDownload).path())!, 'utf8'));
  for (let i = 0; i < 2; i++) {
    expect(restored.presentation.cameras[i].zoom).toBe(document.presentation.cameras[i].zoom);
    restored.presentation.cameras[i].position.forEach((value: number, axis: number) => expect(value).toBeCloseTo(document.presentation.cameras[i].position[axis], 6));
  }
  await expect(page.getByRole('alert')).toHaveCount(0);
});

test('tracks spherical drags in screen direction and preserves full camera orientation', async ({ page }) => {
  type Camera = { position: number[]; target: number[]; up: number[]; zoom: number };
  const saved = async (): Promise<any> => {
    const downloading = page.waitForEvent('download');
    await page.getByRole('button', { name: 'Save session ↗' }).click();
    return JSON.parse(readFileSync((await (await downloading).path())!, 'utf8'));
  };
  await setViews(page, 'cube-sphere');
  const sphere = page.getByLabel('Spherical puzzle', { exact: true });
  const initial = await page.getByTestId('state-digest').textContent();
  const bounds = (await sphere.boundingBox())!;
  let previous = await saved();
  const euclidean = previous.presentation.cameras[0];
  const initialUp = previous.presentation.cameras[1].up;
  for (const [dx, dy] of [[65, 0], [0, -55], [50, 35]]) {
    const before = previous.presentation.cameras[1] as Camera;
    const eye = new Vector3().fromArray(before.position).sub(new Vector3().fromArray(before.target));
    // A surface point facing the camera starts at the center of the screen.
    // Its projected motion must follow horizontal, vertical and diagonal drags,
    // including after previous rotations have changed the camera's up vector.
    const anchor = eye.clone().normalize().multiplyScalar(2.05).add(new Vector3().fromArray(before.target));
    await page.mouse.move(bounds.x + bounds.width / 2, bounds.y + bounds.height / 2);
    await page.mouse.down();
    await page.mouse.move(bounds.x + bounds.width / 2 + dx, bounds.y + bounds.height / 2 + dy, { steps: 8 });
    await page.mouse.up();
    previous = await saved();
    const after = previous.presentation.cameras[1] as Camera;
    const camera = new PerspectiveCamera(35, bounds.width / bounds.height, 0.1, 100);
    camera.position.fromArray(after.position); camera.up.fromArray(after.up); camera.zoom = after.zoom;
    camera.lookAt(new Vector3().fromArray(after.target));
    camera.updateProjectionMatrix(); camera.updateMatrixWorld();
    const projected = anchor.project(camera);
    const movement = [projected.x * bounds.width / 2, -projected.y * bounds.height / 2];
    const length = Math.hypot(...movement), dragLength = Math.hypot(dx, dy);
    expect(length).toBeGreaterThan(5);
    expect((movement[0] * dx + movement[1] * dy) / (length * dragLength)).toBeGreaterThan(0.99999);
    expect(Math.abs(movement[0] * dy - movement[1] * dx) / (length * dragLength)).toBeLessThan(0.001);
    expect(after.target).toEqual(before.target);
    expect(new Vector3().fromArray(after.position).sub(new Vector3().fromArray(after.target)).length()).toBeCloseTo(eye.length(), 7);
    expect(previous.presentation.cameras[0]).toEqual(euclidean);
    await expect(page.getByTestId('state-digest')).toHaveText(initial!);
    await expect(page.getByTestId('diagram-view')).toHaveAttribute('data-selected-piece', '');
  }
  const rotated = previous.presentation.cameras[1] as Camera;
  expect(rotated.up).not.toEqual(initialUp);
  const stopped = (await saved()).presentation.cameras[1] as Camera;
  stopped.position.forEach((value, i) => expect(value).toBeCloseTo(rotated.position[i], 9));
  stopped.up.forEach((value, i) => expect(value).toBeCloseTo(rotated.up[i], 9));
  await setViews(page, 'sphere-diagram');
  const switched = (await saved()).presentation.cameras[0] as Camera;
  switched.position.forEach((value, i) => expect(value).toBeCloseTo(rotated.position[i], 7));
  switched.up.forEach((value, i) => expect(value).toBeCloseTo(rotated.up[i], 7));
  await setViews(page, 'cube-diagram');
  await page.getByLabel('Open session file').setInputFiles({ name: 'trackball-session.json', mimeType: 'application/json', buffer: Buffer.from(JSON.stringify(previous)) });
  await expectViews(page, 'cube-sphere');
  await resting(page);
  const restored = (await saved()).presentation.cameras[1] as Camera;
  restored.position.forEach((value, i) => expect(value).toBeCloseTo(rotated.position[i], 7));
  restored.up.forEach((value, i) => expect(value).toBeCloseTo(rotated.up[i], 7));
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  await expect(page.getByRole('alert')).toHaveCount(0);
});

test('keeps bandage blocking and identity on the spherical surface', async ({ page }) => {
  await page.getByLabel('Puzzle', { exact: true }).selectOption('bandaged-uf-ufr');
  await setViews(page, 'sphere-diagram');
  await expect(page.getByLabel('Spherical puzzle')).toBeVisible();
  const initial = await page.getByTestId('state-digest').textContent();
  await page.getByRole('button', { name: 'Move R', exact: true }).click();
  await expect(page.getByTestId('blocking-evidence')).toContainText('relation.rejected');
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-blocked-pieces', 'bandage/UF-UFR');
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  await page.getByRole('button', { name: 'Select bandage/UF-UFR', exact: true }).click();
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-selected-piece', 'bandage/UF-UFR');
  await expect(page.getByTestId('diagram-view')).toHaveAttribute('data-selected-piece', 'bandage/UF-UFR');
  await page.getByRole('button', { name: 'Move U', exact: true }).click(); await resting(page);
  await page.getByRole('button', { name: 'Move R', exact: true }).click(); await resting(page);
  const reference = JSON.parse(execFileSync('build/native/twisty', ['run', '--definition', 'packages/bandaged/relational-package.json', '--algorithm', 'U R', '--json'], { encoding: 'utf8' }));
  await expect(page.getByTestId('state-digest')).toHaveText(reference.snapshot.stateDigest);
  await page.getByRole('button', { name: '↶ Undo' }).click(); await resting(page);
  await page.getByRole('button', { name: '↷ Redo' }).click(); await resting(page);
  await expect(page.getByTestId('state-digest')).toHaveText(reference.snapshot.stateDigest);
  await expect(page.getByRole('alert')).toHaveCount(0);
});

test('executes algorithms in both views, switches layout, and restores exact state', async ({ page }) => {
  await setViews(page, 'cube-diagram');
  const initial = await page.getByTestId('state-digest').textContent();
  await page.getByLabel('Algorithm', { exact: true }).fill("R U R' U'");
  await page.getByRole('button', { name: 'Play algorithm →' }).click();
  await resting(page);
  const expected = JSON.parse(execFileSync('build/native/twisty', ['run', '--algorithm', "R U R' U'", '--json'], { encoding: 'utf8' }));
  await expect(page.getByTestId('state-digest')).toHaveText(expected.snapshot.stateDigest);
  await setViews(page, ['diagram']);
  await expect(page.locator('[data-view-id="cube"]')).toHaveCount(0);
  await expect(page.getByTestId('state-digest')).toHaveText(expected.snapshot.stateDigest);
  await setViews(page, 'cube-diagram');
  await page.getByLabel('Algorithm', { exact: true }).fill("[R,U]'");
  await page.getByRole('button', { name: 'Play algorithm →' }).click();
  await resting(page);
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  await expect(page.getByTestId('goal-status')).toHaveText('Solved');
});

test('explains and highlights bandaging while preserving blocked state', async ({ page }) => {
  await page.getByLabel('Puzzle', { exact: true }).selectOption('bandaged-uf-ufr');
  await expect(page.getByRole('button', { name: 'Select bandage/UF-UFR', exact: true })).toBeVisible();
  const initial = await page.getByTestId('state-digest').textContent();
  await page.getByRole('button', { name: 'Move R', exact: true }).click();
  await expect(page.getByTestId('blocking-evidence')).toContainText('relation.rejected');
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-blocked-pieces', 'bandage/UF-UFR');
  await expect(page.getByTestId('diagram-view')).toHaveAttribute('data-blocked-pieces', 'bandage/UF-UFR');
  await page.getByRole('button', { name: 'Move U', exact: true }).click(); await resting(page);
  await page.getByRole('button', { name: 'Move R', exact: true }).click(); await resting(page);
  await expect(page.getByTestId('blocking-evidence')).toHaveCount(0);
  await page.getByRole('button', { name: '↶ Undo' }).click(); await resting(page);
  await page.getByRole('button', { name: '↶ Undo' }).click(); await resting(page);
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
});

test('supports transactional rollback and interactive legal prefixes', async ({ page }) => {
  await page.getByLabel('Puzzle', { exact: true }).selectOption('bandaged-uf-ufr');
  const initial = await page.getByTestId('state-digest').textContent();
  await page.getByLabel('Algorithm', { exact: true }).fill('U U\' R');
  await page.getByLabel('Execution policy').selectOption('transactional');
  await page.getByRole('button', { name: 'Play algorithm →' }).click();
  await expect(page.getByTestId('blocking-evidence')).toBeVisible();
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  await page.getByLabel('Execution policy').selectOption('interactive');
  await page.getByRole('button', { name: 'Play algorithm →' }).click(); await resting(page);
  await page.getByRole('button', { name: 'History', exact: true }).click();
  await expect(page.locator('.history-list')).toContainText('2 committed primitives');
});

test('picks a visual port, shares selection, and leaves camera motion outside state', async ({ page }) => {
  await setViews(page, 'cube-diagram');
  const initial = await page.getByTestId('state-digest').textContent();
  // The fixed net's front center is left of its camera target; click a visible port.
  const diagram = await page.getByTestId('diagram-view').boundingBox();
  await page.mouse.click(diagram!.x + diagram!.width * 0.38, diagram!.y + diagram!.height * 0.5);
  await expect(page.getByTestId('diagram-view')).not.toHaveAttribute('data-selected-piece', '');
  const selected = await page.getByTestId('diagram-view').getAttribute('data-selected-piece');
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-selected-piece', selected!);
  const cube = await page.getByTestId('cube-view').boundingBox();
  await page.mouse.move(cube!.x + cube!.width / 2, cube!.y + cube!.height / 2);
  await page.mouse.down(); await page.mouse.move(cube!.x + cube!.width / 2 + 80, cube!.y + cube!.height / 2 + 25, { steps: 8 }); await page.mouse.up();
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
});

test('matches native seeded walks and reloads the exported session', async ({ page }) => {
  await page.getByLabel('Animation speed').selectOption('0');
  await page.getByLabel('Scramble length').fill('8');
  await page.getByRole('button', { name: 'Scramble ↗' }).click(); await resting(page);
  const reference = JSON.parse(execFileSync('build/native/twisty', ['scramble', '--seed', '42', '--length', '8', '--json'], { encoding: 'utf8' }));
  await expect(page.getByTestId('state-digest')).toHaveText(reference.snapshot.stateDigest);
  await expect(page.getByTestId('scramble-output')).toHaveText(reference.scramble.notation);
  const saved = page.waitForEvent('download'); await page.getByRole('button', { name: 'Save session ↗' }).click();
  const download = await saved; const document = readFileSync((await download.path())!, 'utf8');
  await page.getByRole('button', { name: 'Move U', exact: true }).click(); await resting(page);
  await page.getByLabel('Open session file').setInputFiles({ name: 'session.json', mimeType: 'application/json', buffer: Buffer.from(document) });
  await expect(page.getByTestId('state-digest')).toHaveText(reference.snapshot.stateDigest);
  await resting(page);
});

test('imports independent type/domain IDs and named operations without visual support', async ({ page }) => {
  const initialDigest = await page.getByTestId('state-digest').textContent();
  await page.getByLabel('Import definition file').setInputFiles({ name: 'invalid.json', mimeType: 'application/json', buffer: Buffer.from('{}') });
  await expect(page.getByTestId('blocking-evidence')).toContainText('source.invalid');
  await expect(page.getByTestId('state-digest')).toHaveText(initialDigest!);
  await expect(page.locator('canvas')).toHaveCount(2);
  const downloading = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Save session ↗' }).click();
  const originalSession = readFileSync((await (await downloading).path())!, 'utf8');
  const original = JSON.parse(readFileSync('packages/cube3/relational-package.json', 'utf8'));
  // Rename an authoring domain independently of persistent piece IDs/local keys.
  const rename = (value: any): any => {
    if (typeof value === 'string') return value === 'corner' ? 'vertex' : value.replace(/^corner\/corner:/, 'vertex/corner:');
    if (Array.isArray(value)) return value.map(rename);
    if (value && typeof value === 'object') return Object.fromEntries(Object.entries(value).map(([key, child]) =>
      [rename(key), rename(child)]));
    return value;
  };
  const authored = rename(original);
  authored.source.id = 'authored-example'; authored.catalog.puzzleId = 'authored-example';
  authored.source.requests = authored.source.requests.filter((request: { id: string }) => request.id === 'U' || request.id === "U'");
  const names: Record<string, string> = { U: 'turn', "U'": 'back' };
  authored.catalog.requests = Object.fromEntries(Object.entries(authored.catalog.requests)
    .filter(([id]) => id in names).map(([id, value]) => {
      const request = value as { inverse: string; family: string };
      return [names[id], { ...request, inverse: names[request.inverse], family: 'upper' }];
    }));
  for (const domain of authored.source.domains) {
    domain.moves = domain.moves.filter((move: { id: string }) => move.id in names);
    for (const move of domain.moves) move.id = names[move.id];
  }
  for (const request of authored.source.requests) {
    request.id = names[request.id];
    for (const profile of request.profiles) {
      for (const domain of Object.keys(profile.choices)) profile.choices[domain] = names[profile.choices[domain]];
    }
    for (const term of request.relation.all) term.chosen.moves = term.chosen.moves.map((id: string) => names[id]);
  }
  await page.getByLabel('Import definition file').setInputFiles({ name: 'authored.json', mimeType: 'application/json', buffer: Buffer.from(JSON.stringify(authored)) });
  await expect(page.getByRole('alert')).toContainText('No compatible realization');
  await expect(page.locator('canvas')).toHaveCount(0);
  await expect(page.getByRole('button', { name: 'Move R', exact: true })).toHaveCount(0);
  await page.getByRole('button', { name: 'Select corner/UFR', exact: true }).click();
  await expect(page.locator('.piece-detail')).toContainText('UFR');
  await expect(page.locator('.piece-detail dd').nth(1)).toHaveText('UFR');
  const initial = await page.getByTestId('state-digest').textContent();
  await page.getByRole('button', { name: 'Move turn', exact: true }).click();
  await expect(page.getByTestId('state-digest')).not.toHaveText(initial!);
  await page.getByRole('button', { name: 'Move back', exact: true }).click();
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  await page.getByLabel('Open session file').setInputFiles({ name: 'other-session.json', mimeType: 'application/json', buffer: Buffer.from(originalSession) });
  await expect(page.getByTestId('blocking-evidence')).toContainText('definition.digest_mismatch');
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
});

test('imports the Helicopter model, animates both views, and explains jumbling blockers', async ({ page }) => {
  await page.getByLabel('Import definition file').setInputFiles('packages/helicopter/relational-package.json');
  await expect(page.getByRole('button', { name: 'Move UF_ab', exact: true })).toBeVisible();
  await expect(page.getByRole('alert')).toHaveCount(0);
  await expect(page.locator('canvas')).toHaveCount(2);
  await resting(page);
  const initial = await page.getByTestId('state-digest').textContent();
  await page.getByRole('button', { name: 'Move UF_ab', exact: true }).click();
  await resting(page);
  await expect(page.getByTestId('state-digest')).not.toHaveText(initial!);
  const jumbled = await page.getByTestId('state-digest').textContent();
  await page.getByRole('button', { name: 'Move UR_ad', exact: true }).click();
  await expect(page.getByTestId('blocking-evidence')).toContainText('relation.rejected');
  await expect(page.getByTestId('blocking-evidence')).toContainText('center/Ufl');
  await expect(page.getByTestId('state-digest')).toHaveText(jumbled!);
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-blocked-pieces', 'center/Ufl,center/Ufr,corner/FLU,mechanism/UF');
  await expect(page.getByTestId('diagram-view')).toHaveAttribute('data-blocked-pieces', 'center/Ufl,center/Ufr,corner/FLU,mechanism/UF');
  await page.getByRole('button', { name: '↶ Undo' }).click();
  await resting(page);
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
});

test('renders the Helicopter preset through jumbling, inverse, picking, and saved replay', async ({ page }) => {
  await page.getByLabel('Puzzle', { exact: true }).selectOption('helicopter');
  await expect(page.getByRole('alert')).toHaveCount(0);
  await expect(page.locator('canvas')).toHaveCount(2);
  await resting(page);
  const initial = await page.getByTestId('state-digest').textContent();
  await page.screenshot({ path: 'test-results/helicopter-solved.png', fullPage: true });
  await page.getByLabel('Algorithm', { exact: true }).fill('UF_ab UL_af');
  await page.getByRole('button', { name: 'Play algorithm →' }).click();
  await resting(page);
  const reference = JSON.parse(execFileSync('build/native/twisty', ['run', '--definition', 'packages/helicopter/relational-package.json', '--algorithm', 'UF_ab UL_af', '--json'], { encoding: 'utf8' }));
  await expect(page.getByTestId('state-digest')).toHaveText(reference.snapshot.stateDigest);
  await page.screenshot({ path: 'test-results/helicopter-jumbled.png', fullPage: true });
  const canvas = page.getByTestId('cube-view').locator('canvas');
  await canvas.click({ position: { x: 300, y: 180 } });
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-selected-piece', /^(corner|center|mechanism)\//);
  const selected = await page.getByTestId('cube-view').getAttribute('data-selected-piece');
  await expect(page.getByTestId('diagram-view')).toHaveAttribute('data-selected-piece', selected!);
  const downloading = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Save session ↗' }).click();
  const saved = readFileSync((await (await downloading).path())!, 'utf8');
  await page.getByLabel('Algorithm', { exact: true }).fill("(UF_ab UL_af)'");
  await page.getByRole('button', { name: 'Play algorithm →' }).click();
  await resting(page);
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  await page.getByLabel('Open session file').setInputFiles({ name: 'helicopter-session.json', mimeType: 'application/json', buffer: Buffer.from(saved) });
  // File.text() completes asynchronously; wait for the loaded state first.
  await expect(page.getByTestId('state-digest')).toHaveText(reference.snapshot.stateDigest);
  await resting(page);
  await expect(page.getByRole('alert')).toHaveCount(0);
});

test('realizes Helicopter jumbling on a sphere with shared blocking, picking and replay', async ({ page }) => {
  await page.getByLabel('Puzzle', { exact: true }).selectOption('helicopter');
  await setViews(page, 'cube-sphere');
  const sphere = page.getByLabel('Spherical puzzle', { exact: true });
  await expect(sphere).toBeVisible();
  await expect(page.getByTestId('diagram-view')).toHaveAttribute('data-realization-id', 'helicopter-spherical-v1');
  await resting(page);
  const initial = await page.getByTestId('state-digest').textContent();
  await page.screenshot({ path: 'test-results/helicopter-spherical-solved.png', fullPage: true });
  const bounds = await sphere.boundingBox();
  await sphere.click({ position: { x: bounds!.width / 2, y: bounds!.height / 2 } });
  await expect(page.getByTestId('diagram-view')).toHaveAttribute('data-selected-piece', /^(corner|center|mechanism)\//);
  const selected = await page.getByTestId('diagram-view').getAttribute('data-selected-piece');
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-selected-piece', selected!);
  await page.getByRole('button', { name: 'Move UF_ab', exact: true }).click();
  await resting(page);
  const jumbled = await page.getByTestId('state-digest').textContent();
  await page.getByRole('button', { name: 'Move UR_ad', exact: true }).click();
  await expect(page.getByTestId('blocking-evidence')).toContainText('relation.rejected');
  await expect(page.getByTestId('diagram-view')).toHaveAttribute('data-blocked-pieces', 'center/Ufl,center/Ufr,corner/FLU,mechanism/UF');
  await expect(page.getByTestId('state-digest')).toHaveText(jumbled!);
  await page.getByRole('button', { name: '↶ Undo' }).click();
  await resting(page);
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  const algorithm = 'UF_ab DR_ab FR_ad DR_ba UF_ba';
  await page.getByLabel('Algorithm', { exact: true }).fill(algorithm);
  await page.getByRole('button', { name: 'Play algorithm →' }).click();
  await resting(page);
  const reference = JSON.parse(execFileSync('build/native/twisty', ['run', '--definition', 'packages/helicopter/relational-package.json', '--algorithm', algorithm, '--json'], { encoding: 'utf8' }));
  await expect(page.getByTestId('state-digest')).toHaveText(reference.snapshot.stateDigest);
  await setViews(page, 'sphere-diagram');
  await resting(page);
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-realization-id', 'helicopter-spherical-v1');
  await page.screenshot({ path: 'test-results/helicopter-spherical-jumbled.png', fullPage: true });
  const downloading = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Save session ↗' }).click();
  const saved = readFileSync((await (await downloading).path())!, 'utf8');
  await page.getByLabel('Algorithm', { exact: true }).fill(`(${algorithm})'`);
  await page.getByRole('button', { name: 'Play algorithm →' }).click();
  await resting(page);
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  await setViews(page, 'cube-diagram');
  await page.getByLabel('Open session file').setInputFiles({ name: 'helicopter-sphere.json', mimeType: 'application/json', buffer: Buffer.from(saved) });
  await expect(page.getByTestId('state-digest')).toHaveText(reference.snapshot.stateDigest);
  await expectViews(page, 'sphere-diagram');
  await resting(page);
  await expect(page.getByRole('alert')).toHaveCount(0);
});

test('recreates views after context loss and repeated puzzle replacement', async ({ page }) => {
  const initial = await page.getByTestId('state-digest').textContent();
  const recovery = await page.locator('canvas').first().evaluate(async (canvas) => {
    const context = (canvas as HTMLCanvasElement).getContext('webgl2');
    const extension = context?.getExtension('WEBGL_lose_context');
    if (!extension) return 'unsupported';
    return await new Promise<string>((resolve, reject) => {
      const timeout = setTimeout(() => reject(new Error('WebGL context did not restore.')), 5000);
      canvas.addEventListener('webglcontextlost', () => setTimeout(() => extension.restoreContext(), 100), { once: true });
      canvas.addEventListener('webglcontextrestored', () => { clearTimeout(timeout); resolve(context!.isContextLost() ? 'lost' : 'restored'); }, { once: true });
      extension.loseContext();
    });
  });
  expect(recovery).toBe('restored');
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  for (let i = 0; i < 3; i++) {
    await page.getByLabel('Puzzle', { exact: true }).selectOption('bandaged-uf-ufr');
    await page.getByLabel('Puzzle', { exact: true }).selectOption('cube3');
    await expect(page.locator('canvas')).toHaveCount(2);
  }
  await expect(page.getByRole('alert')).toHaveCount(0);
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  await page.screenshot({ path: 'test-results/simulator.png', fullPage: true });
});
