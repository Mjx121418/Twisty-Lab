import { test, expect } from '@playwright/test';
import { readFileSync } from 'node:fs';
import { execFileSync } from 'node:child_process';

test.beforeEach(async ({ page }) => {
  await page.goto('./');
  await expect(page.getByTestId('state-digest')).toHaveText(/[0-9a-f]{64}/);
  await expect(page.locator('canvas')).toHaveCount(2);
  await expect(page.getByRole('alert')).toHaveCount(0);
});
async function resting(page: import('@playwright/test').Page): Promise<void> {
  await expect(page.getByTestId('app')).toHaveAttribute('data-animating', 'false');
  const digest = await page.getByTestId('state-digest').textContent();
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-state-digest', digest!);
  await expect(page.getByTestId('diagram-view')).toHaveAttribute('data-state-digest', digest!);
}

test('runs the same cube on a sphere, preserves state across views, and restores presentation', async ({ page }) => {
  const initial = await page.getByTestId('state-digest').textContent();
  await page.getByLabel('Geometry views').selectOption('cube-sphere');
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
  await page.getByLabel('Geometry views').selectOption('sphere-diagram');
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
  expect(document.presentation.viewPair).toBe('sphere-diagram');
  document.presentation.cameras = [
    { position: [4, 3, 7], target: [0, 0, 0], zoom: 1.2 },
    { position: [2, 1, 15], target: [1.5, 0, 0], zoom: 1.3 },
  ];
  await page.getByLabel('Geometry views').selectOption('cube-diagram');
  await page.getByLabel('Algorithm', { exact: true }).fill("(U R F D L B)'");
  await page.getByRole('button', { name: 'Play algorithm →' }).click();
  await resting(page);
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  await page.getByLabel('Open session file').setInputFiles({ name: 'sphere-session.json', mimeType: 'application/json', buffer: Buffer.from(JSON.stringify(document)) });
  await expect(page.getByTestId('state-digest')).toHaveText(reference.snapshot.stateDigest);
  await expect(page.getByLabel('Geometry views')).toHaveValue('sphere-diagram');
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

test('keeps bandage blocking and identity on the spherical surface', async ({ page }) => {
  await page.getByLabel('Puzzle', { exact: true }).selectOption('bandaged-uf-ufr');
  await page.getByLabel('Geometry views').selectOption('sphere-diagram');
  await expect(page.getByLabel('Spherical puzzle')).toBeVisible();
  const initial = await page.getByTestId('state-digest').textContent();
  await page.getByRole('button', { name: 'Move R', exact: true }).click();
  await expect(page.getByTestId('blocking-evidence')).toContainText('footprint.partial_overlap');
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-blocked-pieces', 'bandage/UF-UFR');
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  await page.getByRole('button', { name: 'Select bandage/UF-UFR', exact: true }).click();
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-selected-piece', 'bandage/UF-UFR');
  await expect(page.getByTestId('diagram-view')).toHaveAttribute('data-selected-piece', 'bandage/UF-UFR');
  await page.getByRole('button', { name: 'Move U', exact: true }).click(); await resting(page);
  await page.getByRole('button', { name: 'Move R', exact: true }).click(); await resting(page);
  const reference = JSON.parse(execFileSync('build/native/twisty', ['run', '--definition', 'packages/bandaged/definition.json', '--algorithm', 'U R', '--json'], { encoding: 'utf8' }));
  await expect(page.getByTestId('state-digest')).toHaveText(reference.snapshot.stateDigest);
  await page.getByRole('button', { name: '↶ Undo' }).click(); await resting(page);
  await page.getByRole('button', { name: '↷ Redo' }).click(); await resting(page);
  await expect(page.getByTestId('state-digest')).toHaveText(reference.snapshot.stateDigest);
  await expect(page.getByRole('alert')).toHaveCount(0);
});

test('executes algorithms in both views, switches layout, and restores exact state', async ({ page }) => {
  const initial = await page.getByTestId('state-digest').textContent();
  await page.getByLabel('Algorithm', { exact: true }).fill("R U R' U'");
  await page.getByRole('button', { name: 'Play algorithm →' }).click();
  await resting(page);
  const expected = JSON.parse(execFileSync('build/native/twisty', ['run', '--algorithm', "R U R' U'", '--json'], { encoding: 'utf8' }));
  await expect(page.getByTestId('state-digest')).toHaveText(expected.snapshot.stateDigest);
  await page.getByRole('button', { name: 'Diagram', exact: true }).click();
  await expect(page.getByTestId('cube-view')).not.toBeVisible();
  await expect(page.getByTestId('state-digest')).toHaveText(expected.snapshot.stateDigest);
  await page.getByRole('button', { name: 'Both views' }).click();
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
  await expect(page.getByTestId('blocking-evidence')).toContainText('footprint.partial_overlap');
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
  const authored = JSON.parse(readFileSync('packages/cube3/definition.json', 'utf8'));
  delete authored.definitionDigest; delete authored.symmetry; delete authored.provenance;
  authored.puzzleId = 'authored-example';
  authored.pieceTypes.find((type: { id: string }) => type.id === 'corner').id = 'vertex';
  for (const piece of authored.pieces) if (piece.type === 'corner') piece.type = 'vertex';
  authored.operations = authored.operations.filter((operation: { family: string }) => operation.family === 'U');
  for (const operation of authored.operations) {
    operation.id = operation.id === 'U' ? 'turn' : 'back';
    operation.inverse = operation.inverse === 'U' ? 'turn' : 'back';
    operation.family = 'upper';
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
  await page.getByLabel('Import definition file').setInputFiles('packages/helicopter/definition.json');
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
  await expect(page.getByTestId('blocking-evidence')).toContainText('placement.blocked');
  await expect(page.getByTestId('blocking-evidence')).toContainText('center/Ufl');
  await expect(page.getByTestId('state-digest')).toHaveText(jumbled!);
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-blocked-pieces', 'center/Ufl');
  await expect(page.getByTestId('diagram-view')).toHaveAttribute('data-blocked-pieces', 'center/Ufl');
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
  const reference = JSON.parse(execFileSync('build/native/twisty', ['run', '--definition', 'packages/helicopter/definition.json', '--algorithm', 'UF_ab UL_af', '--json'], { encoding: 'utf8' }));
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
